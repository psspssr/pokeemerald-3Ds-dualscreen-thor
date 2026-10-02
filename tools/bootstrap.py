#!/usr/bin/env python3
"""Build tree, native library and APK for the Android port.

    python3 tools/bootstrap.py                  # -> build/upstream
    python3 tools/bootstrap.py --make -j4       # and libemerald.so + RomFS
    python3 tools/bootstrap.py --make --apk     # and the debug APK
    python3 tools/bootstrap.py --clean --make   # start again from the pinned commits

1. Runs origin's own bootstrap (origin/tools/bootstrap.py --dir build/upstream):
   pinned pret/pokeemerald, origin's patches, origin's 3ds_port/, tools/ and
   builder/ overlaid.
2. Checks and applies patches/android/*.patch in filename order, in a small
   temporary tree containing only their source files. Recorded before/after
   hashes make repeat builds exact; unexpected local edits or shifted patch
   contexts stop the build. Changed patches refresh the generated sources
   through origin's bootstrap and invalidate the native configuration stamp.
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
import re
import shutil
import stat
import subprocess
import sys
import tempfile
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
MARKER_VERSION = 2


class PatchError(RuntimeError):
    pass


def run(cmd, cwd=None, env=None):
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, check=True, env=env)


def validate_source_path(rel: str) -> None:
    parts = rel.split("/")
    if (not re.fullmatch(r"[A-Za-z0-9_./+-]+", rel)
            or any(part in ("", ".", "..", ".git", "build", "romfs", "android") for part in parts)
            or parts[0].startswith(".emerald")):
        raise PatchError("unsafe or unsupported Android patch path: %s" % rel)


def source_path(tree: Path, rel: str) -> Path:
    """Patches are source-only: no escapes, symlinks or generated build state."""
    validate_source_path(rel)
    path = tree
    for part in rel.split("/"):
        path = path / part
        if path.is_symlink():
            raise PatchError("Android patch path is a symlink: %s" % rel)
    if path.exists() and not path.is_file():
        raise PatchError("Android patch path is not a regular file: %s" % rel)
    return path


def patch_files(patch: Path) -> list[str]:
    """Ask Git for every path, accepting unified/git text diffs and file modes.

    Binary patches, renames and symlinks are unnecessary for engine hooks and
    rejected. Git's own parser accounts for every file, including deletions.
    """
    try:
        text = patch.read_text(encoding="utf-8")
    except UnicodeError as exc:
        raise PatchError("%s: Android patches must be UTF-8 text" % patch.name) from exc
    for line in text.splitlines():
        if line.startswith(("GIT binary patch", "Binary files ", "rename ", "copy ")):
            raise PatchError("%s: only regular-file text changes are supported" % patch.name)
        if re.match(r"(?:new file|deleted file|old|new) mode ", line):
            if line.rsplit(" ", 1)[-1] not in ("100644", "100755"):
                raise PatchError("%s: symlink/submodule modes are unsupported" % patch.name)
        if line.startswith("diff --git "):
            match = re.fullmatch(r"diff --git a/(\S+) b/(\S+)", line)
            if not match or match[1] != match[2]:
                raise PatchError("%s: renamed or quoted paths are unsupported" % patch.name)
    result = subprocess.run(["git", "apply", "--numstat", "-z", str(patch.resolve())],
                            cwd=ROOT, capture_output=True)
    if result.returncode:
        raise PatchError("%s: invalid patch: %s" % (patch.name, result.stderr.decode().strip()))
    paths = []
    for record in result.stdout.split(b"\0"):
        if not record:
            continue
        fields = record.split(b"\t", 2)
        if len(fields) != 3 or not fields[2]:
            raise PatchError("%s: unsupported path record" % patch.name)
        rel = fields[2].decode("utf-8")
        validate_source_path(rel)
        if rel in paths:
            raise PatchError("%s: duplicate diff for %s" % (patch.name, rel))
        paths.append(rel)
    if not paths:
        raise PatchError("%s: patch contains no source changes" % patch.name)
    return paths


def digest(patches: list[Path]) -> str:
    h = hashlib.sha256()
    for p in patches:
        h.update(p.name.encode() + b"\0")
        h.update(p.read_bytes())
        h.update(b"\0")
    return h.hexdigest()


def file_state(tree: Path, rel: str) -> dict | None:
    path = source_path(tree, rel)
    if not path.exists():
        return None
    return {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "mode": 0o755 if path.stat().st_mode & stat.S_IXUSR else 0o644}


def read_patch_state(tree: Path) -> dict | None:
    marker = tree / MARKER
    if not marker.exists():
        return None
    try:
        have = json.loads(marker.read_text())
        # Older builds used the empty series; there is nothing to undo.
        if have == {"digest": hashlib.sha256(b"").hexdigest(), "created": []}:
            return None
        if have["version"] != MARKER_VERSION or not isinstance(have["files"], dict):
            raise ValueError("unknown version")
        for rel, images in have["files"].items():
            source_path(tree, rel)
            if set(images) != {"before", "after"}:
                raise ValueError("invalid image record")
            for image in images.values():
                if image is not None and (set(image) != {"sha256", "mode"}
                        or not re.fullmatch(r"[0-9a-f]{64}", image["sha256"])
                        or image["mode"] not in (0o644, 0o755)):
                    raise ValueError("invalid file state")
        if not all(isinstance(have[key], str) for key in ("digest", "origin")):
            raise ValueError("invalid identity")
        return have
    except (ValueError, KeyError, TypeError) as exc:
        raise PatchError("unverifiable Android patch state; rebuild this generated tree with --clean") from exc


def check_applied_sources(tree: Path, have: dict) -> None:
    for rel, images in have["files"].items():
        if file_state(tree, rel) != images["after"]:
            raise PatchError("generated source %s changed after patching; preserve any edits, then use --clean" % rel)


def strict_apply(tree: Path, patch: Path, extra: tuple[str, ...] | list[str] = ()) -> None:
    command = ["git", "apply", "--verbose", "--whitespace=nowarn", *extra, str(patch.resolve())]
    check = subprocess.run(command[:2] + ["--check"] + command[2:], cwd=tree,
                           capture_output=True, text=True)
    detail = check.stdout + check.stderr
    if check.returncode or re.search(r"\(offset [+-]?\d+ lines?\)", detail):
        raise PatchError("%s does not match the generated upstream exactly; rebase the patch:\n%s"
                         % (patch.name, detail.strip()))
    applied = subprocess.run(command, cwd=tree, capture_output=True, text=True)
    if applied.returncode:
        raise PatchError("%s failed: %s" % (patch.name, applied.stderr.strip()))


def apply_patches(tree: Path, patches: list[Path], previous: dict | None = None,
                  preserve_times: dict[str, tuple[int, int]] | None = None) -> dict:
    """Validate the whole ordered series before replacing any source file.

    Origin recopies its overlay on every run, while patched pret files remain.
    Exact recorded hashes distinguish these cases. Reverse only verified final
    files in the temporary tree, then apply the complete series once to its base.
    """
    paths = sorted({rel for patch in patches for rel in patch_files(patch)})
    if previous and set(paths) != set(previous["files"]):
        raise PatchError("Android patch file list differs from its recorded state; use --clean")
    if not paths:
        return {}
    with tempfile.TemporaryDirectory(prefix="emerald-android-patches-", dir=tree.parent) as name:
        stage = Path(name)
        subprocess.run(["git", "init", "-q", stage], check=True)
        reverse = []
        for rel in paths:
            current = file_state(tree, rel)
            if previous:
                images = previous["files"][rel]
                if current == images["after"] and current != images["before"]:
                    reverse.append("--include=" + rel)
                elif current != images["before"]:
                    raise PatchError("generated source %s matches neither recorded image; use --clean" % rel)
            if current is not None:
                target = stage / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(tree / rel, target)
        if reverse:
            for patch in reversed(patches):
                strict_apply(stage, patch, ["--reverse", *reverse])
        before = {rel: file_state(stage, rel) for rel in paths}
        if previous and any(before[rel] != previous["files"][rel]["before"] for rel in paths):
            raise PatchError("Android patch base differs from its recorded source; use --clean")
        for patch in patches:
            strict_apply(stage, patch)
            print("bootstrap: checked %s" % patch.name)
        images = {rel: {"before": before[rel], "after": file_state(stage, rel)} for rel in paths}
        if previous and images != previous["files"]:
            raise PatchError("Android patch result differs from its recorded state; use --clean")
        for rel, image in images.items():
            if file_state(tree, rel) == image["after"]:
                continue  # Preserve source timestamps on a warm build.
            target = source_path(tree, rel)
            if image["after"] is None:
                target.unlink()
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(stage / rel, target)
                if preserve_times and rel in preserve_times:
                    # Origin recopies its overlay even on a warm build. Its
                    # patched result is identical: retain the previous mtime.
                    os.utime(target, ns=preserve_times[rel])
        return images


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
        origin = check_origin(ROOT)
    except OriginError as exc:
        raise SystemExit("bootstrap: %s" % exc) from exc

    tree = args.dir.resolve()
    patches = sorted(PATCHES.glob("*.patch"))
    marker = tree / MARKER
    want = {"version": MARKER_VERSION, "digest": digest(patches), "origin": origin["commit"]}
    # Reject unsupported patches before any generated source is refreshed.
    for patch in patches:
        patch_files(patch)
    have = None if args.clean else read_patch_state(tree)
    previous_times = {}
    if have:
        check_applied_sources(tree, have)
        for rel, images in have["files"].items():
            if images["after"] is not None:
                info = (tree / rel).stat()
                previous_times[rel] = (info.st_atime_ns, info.st_mtime_ns)
    stale = have is not None and any(have[key] != want[key] for key in ("digest", "origin"))
    if stale:
        # This is only the disposable build tree. Origin resets its tracked
        # pret files and recopies its overlay; remove our previous additions.
        print("bootstrap: Android patches/upstream changed, refreshing generated sources")
        (tree / ORIGIN_MARKER).unlink(missing_ok=True)
        marker.unlink(missing_ok=True)
        for rel, images in have["files"].items():
            if images["before"] is None:
                source_path(tree, rel).unlink(missing_ok=True)
    if stale or args.clean or (have is None and patches):
        # Source copies preserve mtimes; never reuse objects made with an old
        # hook/configuration even if a restored upstream file is older.
        (tree / "3ds_port/build/config").unlink(missing_ok=True)

    cmd = [args.python, ORIGIN / "tools" / "bootstrap.py", "--dir", tree, "--python", args.python]
    if args.clean:
        cmd.append("--clean")
    run(cmd)

    want["files"] = apply_patches(tree, patches, have if not stale else None,
                                 previous_times if not stale else None)
    temporary = marker.with_name(marker.name + ".tmp")
    temporary.write_text(json.dumps(want, indent=1, sort_keys=True) + "\n")
    temporary.replace(marker)

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
    try:
        sys.exit(main())
    except PatchError as exc:
        sys.exit("bootstrap: %s" % exc)
