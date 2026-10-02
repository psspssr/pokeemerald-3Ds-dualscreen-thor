#!/usr/bin/env python3
"""Build tree, native library and APK for the Android port.

    python3 tools/bootstrap.py                  # -> build/upstream
    python3 tools/bootstrap.py --make -j4       # and libemerald.so + RomFS
    python3 tools/bootstrap.py --make --apk     # and the debug APK
    python3 tools/bootstrap.py --clean --make   # start again from the pinned commits

1. Runs origin's own bootstrap (origin/tools/bootstrap.py --dir build/upstream):
   pinned pret/pokeemerald, origin's patches, origin's 3ds_port/, tools/ and
   builder/ overlaid.
2. Applies patches/android/*.patch, in order, on top of that. A patch (or a
   file of one) that is already in place is skipped, so re-running is cheap;
   when the series itself changes, the tree is reset through origin's
   bootstrap first (digest marker build/upstream/.emerald-android-patches).
3. Mirrors android/ (minus app/) to build/upstream/android/ and tools/ to
   build/upstream/android/tools/.
4. With --make: `make tools generated` (the decomp's host tools), then
   android/native/Makefile from 3ds_port, which leaves libemerald.so,
   libemeraldboot.so and the RomFS in build/android-out/.
5. With --apk: Gradle in android/app, given -Pemerald.nativeOut=build/android-out.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from check_origin import OriginError, check_origin

ROOT = Path(__file__).resolve().parents[1]
ORIGIN = ROOT / "origin"
ANDROID = ROOT / "android"
PATCHES = ROOT / "patches" / "android"
# Copied into the tree; app/ is built in place by Gradle.
ANDROID_PARTS = ["toolchain", "native", "shim", "gpu", "host"]
ORIGIN_MARKER = ".emerald3ds-patches"
MARKER = ".emerald-android-patches"


def run(cmd, cwd=None, env=None):
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, check=True, env=env)


def quiet(cmd, cwd) -> bool:
    return subprocess.run([str(c) for c in cmd], cwd=cwd, capture_output=True).returncode == 0


def patch_files(patch: Path) -> tuple[list[str], list[str]]:
    """Paths a patch touches, and the ones it creates."""
    touched, created = [], []
    lines = patch.read_text(encoding="utf-8", errors="replace").splitlines()
    for i, line in enumerate(lines):
        if line.startswith("+++ b/"):
            path = line[6:].split("\t")[0]
            touched.append(path)
            if i > 0 and lines[i - 1].startswith("--- /dev/null"):
                created.append(path)
    return touched, created


def digest(patches: list[Path]) -> str:
    h = hashlib.sha256()
    for p in patches:
        h.update(p.name.encode())
        h.update(p.read_bytes())
    return h.hexdigest()


def apply_patches(tree: Path, patches: list[Path]) -> None:
    """Apply each file of each patch unless it is already applied. Files under
    the overlaid directories are restored by every bootstrap run, files of
    the decomp are not, so the decision is made per file."""
    for patch in patches:
        touched, _ = patch_files(patch)
        applied = skipped = 0
        for path in dict.fromkeys(touched):
            include = "--include=" + path
            if quiet(["git", "apply", "--check", "--whitespace=nowarn", include, patch], tree):
                run(["git", "apply", "--whitespace=nowarn", include, patch], cwd=tree)
                applied += 1
            elif quiet(["git", "apply", "--check", "-R", "--whitespace=nowarn", include, patch], tree):
                skipped += 1
            else:
                raise SystemExit("bootstrap: %s does not apply to %s (neither forward nor reversed)"
                                 % (patch.name, path))
        print("bootstrap: %s: %d file(s) applied, %d already in place" % (patch.name, applied, skipped))


def mirror(src: Path, dst: Path, keep: tuple[str, ...] = ()) -> None:
    """Copy src over dst and delete what src no longer has. Timestamps are
    preserved so make only rebuilds what changed. `keep` names top-level
    entries of dst that are build products, never deleted."""
    ignore = shutil.ignore_patterns("__pycache__", "*.pyc", "build", ".gradle", ".cxx")
    shutil.copytree(src, dst, dirs_exist_ok=True, ignore=ignore)
    for path in sorted(dst.rglob("*"), key=lambda p: len(p.parts), reverse=True):
        rel = path.relative_to(dst)
        if rel.parts[0] in keep or "__pycache__" in rel.parts:
            continue
        if not (src / rel).exists() and not (src / rel).is_symlink():
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path)
            else:
                path.unlink()


def find_ndk() -> str | None:
    for var in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT"):
        if os.environ.get(var):
            return os.environ[var]
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", type=Path, default=ROOT / "build" / "upstream")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "android-out",
                    help="where the native library and RomFS go (Gradle's emerald.nativeOut)")
    ap.add_argument("--clean", action="store_true", help="reset the tree to the pinned commits first")
    ap.add_argument("--make", action="store_true", help="build the host tools, libemerald.so and the RomFS")
    ap.add_argument("--apk", action="store_true", help="also build the APK (implies --make)")
    ap.add_argument("--release", action="store_true",
                    help="engine-only RomFS and assembleRelease (origin's release: data from a pack)")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--python", default=sys.executable, help="Python the build calls (PYTHON=)")
    ap.add_argument("--ndk", default=find_ndk(), help="Android NDK r27 (default: $ANDROID_NDK_HOME, "
                    "then $ANDROID_HOME/ndk/<pinned version>)")
    args = ap.parse_args()
    if args.apk:
        args.make = True

    try:
        check_origin(ROOT)
    except OriginError as exc:
        raise SystemExit("bootstrap: %s" % exc) from exc

    tree = args.dir.resolve()
    patches = sorted(PATCHES.glob("*.patch"))
    marker = tree / MARKER
    want = {"digest": digest(patches), "created": sorted({c for p in patches for c in patch_files(p)[1]})}
    have = json.loads(marker.read_text()) if marker.exists() else None
    stale = have is not None and have.get("digest") != want["digest"]
    if stale and (tree / ORIGIN_MARKER).exists():
        # The series changed: let origin's bootstrap reset the tree, and drop
        # the files the previous series created so the new one applies.
        print("bootstrap: patches/android changed, resetting the tree")
        (tree / ORIGIN_MARKER).unlink()
        for rel in have.get("created", []):
            if (tree / rel).exists():
                (tree / rel).unlink()

    cmd = [args.python, ORIGIN / "tools" / "bootstrap.py", "--dir", tree, "--python", args.python]
    if args.clean:
        cmd.append("--clean")
    run(cmd)

    apply_patches(tree, patches)
    marker.write_text(json.dumps(want, indent=1) + "\n")

    (tree / "android").mkdir(exist_ok=True)
    for part in ANDROID_PARTS:
        if (ANDROID / part).is_dir():
            mirror(ANDROID / part, tree / "android" / part, keep=("build",) if part == "native" else ())
        elif (tree / "android" / part).exists():
            shutil.rmtree(tree / "android" / part)
    if (ROOT / "tools").is_dir():
        mirror(ROOT / "tools", tree / "android" / "tools")
    print("bootstrap: Android tree ready at %s" % tree)

    if not args.make:
        return 0

    jobs = "-j%d" % args.jobs
    run(["make", "tools", jobs], cwd=tree)
    run(["make", "generated", jobs], cwd=tree)
    out = args.out.resolve()
    make = ["make", "-C", tree / "3ds_port", "-f", "../android/native/Makefile", jobs,
            "PYTHON=%s" % args.python, "ANDROID_OUT=%s" % out,
            "package-release" if args.release else "package"]
    if args.ndk:
        make.append("NDK=%s" % Path(args.ndk).resolve())
    run(make)
    print("bootstrap: native outputs in %s" % out)

    if args.apk:
        app = ANDROID / "app"
        gradlew = app / "gradlew"
        if not gradlew.exists():
            raise SystemExit("bootstrap: --apk needs the Gradle project in android/app (no gradlew there)")
        task = "assembleRelease" if args.release else "assembleDebug"
        run(["bash", gradlew, task, "-Pemerald.nativeOut=%s" % out, "--no-daemon", "--stacktrace"], cwd=app)
    return 0


if __name__ == "__main__":
    sys.exit(main())
