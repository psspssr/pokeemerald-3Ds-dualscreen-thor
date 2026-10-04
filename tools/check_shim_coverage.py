#!/usr/bin/env python3
"""What origin's native units need from the Android SDK layer, and what is missing.

    python3 tools/check_shim_coverage.py            # report, exit 1 on gaps
    python3 tools/check_shim_coverage.py --list     # the SDK identifiers used
    python3 tools/check_shim_coverage.py --json     # machine-readable report

Origin's native units (the SOURCES of origin/3ds_port/Makefile and the
native rules of full.mk, i.e. those compiled with $(CPPFLAGS) $(CFLAGS)) are
the only code that sees libctru, citro3d and citro2d. This lists every SDK
identifier they use and checks it against the Android layer:

  undeclared   not found in android/shim/include or android/gpu/include
  undefined    called (or, once the objects exist, referenced) by a native
               unit but not defined in the built library
               (build/upstream/3ds_port/emerald3ds.elf, via nm)

With --headers-only this is an advisory source check, not link or behavior
validation. With all native objects, the symbol check compares their undefined
symbols with the library, SDK object definitions from the actual link response
file, and NDK exports. SDK definitions can be absent from the final library when
--gc-sections removes unused 3DS startup code together with its dependencies.
Before that, coverage is estimated from
call sites in the source. The native build's check_link.py remains authoritative
for the final ELF, relocation and load-address requirements.
SDK identifiers are recognised by libctru/citro naming (C3D_*, gfx*, svc*,
KEY_*, ...); see SDK_PATTERN.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NDK_VERSION = "27.2.12479018"

SDK_UPPER = (r"(?:C3D|C3Di|C2D|C2Di|Tex3DS|GPU|GPUCMD|GX|GSP|GSPGPU|DVLB|DVLE|DVLP|NDSP|APT|APTHOOK|"
             r"HID|KEY|GFX|MEMOP|MEMPERM|MEMSTATE|RESLIMIT|RESET|USERBREAK|SYSCLOCK|OS|FS|FSUSER|ARCHIVE|"
             r"CFG|CFGU|PTMU|MCUHWC|CUR|RL|RS|RM|RD|RC|Mtx|FVec3|FVec4|Quat|LightLock|LightEvent|"
             r"LightSemaphore|RecursiveLock|CondVar|ROMFS|Result)_\w*")
SDK_LOWER = (r"(?:gfx|gxCmd|gsp|gspgpu|hid|apt|ndsp|romfs|console|linear|vram|mappable|svc|os|thread|cfgu|"
             r"ptmu|mcuHwc|srv|psm|irrst|shader|shaderProgram|shaderInstance)[A-Z0-9]\w*")
SDK_NAMES = (r"(?:Handle|Result|Thread|PrintConsole|ndspWaveBuf|aptHookCookie|devoptab_t|devoptab_list|"
             r"R_FAILED|R_SUCCEEDED|MAKERESULT|BIT|u8|u16|u32|u64|s8|s16|s32|s64|vu8|vu16|vu32|vu64|vs32|"
             r"fake_heap_start|fake_heap_end|__ctru_\w+|__system_\w+|__C3D_\w+|Tex3DS\w*|C3D\w+|C2D\w+|"
             r"CTRU_\w+|DVLB_s|DVLE_s|DVLP_s|shaderProgram_s|shaderInstance_s)")
SDK_PATTERN = re.compile(r"^(?:%s|%s|%s)$" % (SDK_UPPER, SDK_LOWER, SDK_NAMES))
IDENT = re.compile(r"\b[A-Za-z_]\w*\b")


def strip_c(text: str) -> str:
    # Strings must be recognized before their contents can look like comments
    # (for example "https://..."). Keep line boundaries for declaration scans.
    pattern = r'''//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*' '''.rstrip()
    return re.sub(pattern, lambda m: "\n" * m[0].count("\n") or " ", text, flags=re.S)


def native_units(port: Path) -> dict[Path, str]:
    """The native translation units and their objects (relative to 3ds_port),
    read from origin's makefiles."""
    units: dict[str, str] = {}
    makefile = re.sub(r"\\\n[ \t]*", " ", (port / "Makefile").read_text(encoding="utf-8"))
    for line in makefile.splitlines():
        m = re.match(r"^SOURCES\s*[:+]?=\s*(.*)$", line)
        if m:
            for src in m.group(1).split("#", 1)[0].split():
                if not re.fullmatch(r"src/[\w./-]+\.c", src):
                    raise ValueError(f"cannot resolve native SOURCES entry {src!r}; update this checker")
                units[src] = re.sub(r"^src/(.*)\.c$", r"build/\1.o", src)
    lines = (port / "full.mk").read_text(encoding="utf-8").splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"^(build/\S+\.o):\s*(src/\S+\.c)\b", line)
        if not m:
            continue
        recipe = []
        for follow in lines[i + 1:]:
            if not follow.startswith("\t"):
                break
            recipe.append(follow)
        if any("$(CPPFLAGS) $(CFLAGS)" in r for r in recipe):
            units[m.group(2)] = m.group(1)
    if not units:
        raise ValueError("no native units found; origin's Makefile structure may have changed")
    return {port / src: obj for src, obj in units.items()}


def local_headers(port: Path, units: list[Path]) -> list[Path]:
    """Follow quoted includes recursively, including indirect SDK users."""
    found: list[Path] = []
    pending = list(units)
    seen = {p.resolve() for p in units}
    while pending:
        unit = pending.pop()
        for name in re.findall(r'#\s*include\s+"([^"]+)"', unit.read_text(encoding="utf-8", errors="ignore")):
            for base in (unit.parent, port / "include"):
                path = base / name
                if path.is_file():
                    resolved = path.resolve()
                    if resolved.is_relative_to(port.resolve()) and resolved not in seen:
                        seen.add(resolved)
                        found.append(path)
                        pending.append(path)
                    break
    return found


def sdk_identifiers(files: list[Path]) -> tuple[set[str], set[str]]:
    used: set[str] = set()
    called: set[str] = set()
    for path in files:
        text = strip_c(path.read_text(encoding="utf-8", errors="ignore"))
        for name in IDENT.findall(text):
            name = name.removeprefix("__real_")
            if SDK_PATTERN.match(name):
                used.add(name)
        for name in re.findall(r"\b([A-Za-z_]\w*)\s*\(", text):
            name = name.removeprefix("__real_")
            if SDK_PATTERN.match(name):
                called.add(name)
    return used, called


def self_declared(files: list[Path]) -> set[str]:
    """Names origin declares itself at file scope (`extern u32 __ctru_heap;`,
    its own __system_allocateHeaps): they need a definition, not a header."""
    names: set[str] = set()
    for path in files:
        text = strip_c(path.read_text(encoding="utf-8", errors="ignore"))
        names.update(re.findall(r"#\s*define\s+([A-Za-z_]\w*)", text))
        for line in text.splitlines():
            if not line or line[0] in " \t#{}":
                continue
            names.update(re.findall(r"\b([A-Za-z_]\w*)\s*(?=[\[=;,(])", line))
    return names


def wrapped_references(tree: Path, needed: set[str]) -> set[str]:
    """Apply only the --wrap names recorded by the actual Android link rule."""
    stamp = tree / "3ds_port/build/android.wrap"
    if not stamp.is_file():
        return needed
    wrapped = set(stamp.read_text().split())
    if any(not re.fullmatch(r"[A-Za-z_]\w*", name) for name in wrapped):
        raise ValueError("invalid linked wrapper list: %s" % stamp)
    return {name.removeprefix("__real_") if name.startswith("__real_") and name[7:] in wrapped
            else "__wrap_" + name if name in wrapped else name for name in needed}


def header_index(dirs: list[Path]) -> tuple[set[str], set[str]]:
    """Every identifier in the headers, and those that are macros or inline
    functions (need no definition in the library)."""
    tokens: set[str] = set()
    inline: set[str] = set()
    for d in dirs:
        for path in sorted(d.rglob("*.h")) if d.exists() else []:
            text = strip_c(path.read_text(encoding="utf-8", errors="ignore"))
            tokens.update(IDENT.findall(text))
            inline.update(re.findall(r"#\s*define\s+([A-Za-z_]\w*)", text))
            inline.update(re.findall(r"\binline\b[^;{]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{", text, flags=re.S))
    return tokens, inline


def nm(path: Path, *flags: str, tool: str = "arm-linux-gnueabi-nm") -> set[str]:
    out = subprocess.run([tool, *flags, str(path)], capture_output=True, text=True,
                         errors="ignore")
    if out.returncode != 0:
        raise RuntimeError(f"{tool} failed for {path}: {out.stderr.strip()}")
    return {line.split()[-1].split("@")[0] for line in out.stdout.splitlines() if line.split()}


def linked_sdk_objects(tree: Path) -> list[Path]:
    """SDK inputs to this link, excluding stale objects left by earlier builds."""
    port = (tree / "3ds_port").resolve()
    response = port / "build/link.rsp"
    if not response.is_file():
        raise ValueError(f"native link response file missing: {response}; rebuild the native library")
    sdk_roots = [port / "build/android" / component for component in ("shim", "gpu", "host")]
    objects: list[Path] = []
    for token in shlex.split(response.read_text(encoding="utf-8")):
        path = Path(token)
        if path.suffix != ".o":
            continue
        path = (path if path.is_absolute() else port / path).resolve()
        if not any(path.is_relative_to(root) for root in sdk_roots):
            continue
        if not path.is_file():
            raise ValueError(f"linked SDK object missing: {path}; rebuild the native library")
        if path not in objects:
            objects.append(path)
    return objects


def find_ndk(explicit: Path | None = None) -> Path:
    if explicit:
        return explicit
    for var in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT"):
        if os.environ.get(var):
            return Path(os.environ[var])
    for sdk in (os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT"),
                Path.home() / "Android/Sdk", Path.home() / "android-sdk"):
        if sdk:
            candidate = Path(sdk) / "ndk" / NDK_VERSION
            if candidate.is_dir():
                return candidate
    raise ValueError("NDK not found; set --ndk or ANDROID_NDK_HOME for the symbol check")


def ndk_exports(ndk: Path, tool: str) -> set[str]:
    libraries = ndk / "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/arm-linux-androideabi/28"
    if not libraries.is_dir():
        raise ValueError(f"Android API 28 ARM libraries not found at {libraries}")
    names: set[str] = set()
    for name in ("libc.so", "libm.so", "libdl.so", "liblog.so", "libandroid.so", "libEGL.so", "libGLESv3.so", "libaaudio.so"):
        names |= nm(libraries / name, "-D", "--defined-only", tool=tool)
    return names


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--origin", type=Path, default=ROOT / "origin")
    ap.add_argument("--android", type=Path, default=ROOT / "android")
    ap.add_argument("--tree", type=Path, default=ROOT / "build" / "upstream",
                    help="the bootstrapped tree, for the built objects and library")
    ap.add_argument("--lib", type=Path, default=None,
                    help="the unstripped library (default: <tree>/3ds_port/emerald3ds.elf)")
    ap.add_argument("--headers-only", action="store_true", help="advisory declarations check; skip library/objects")
    ap.add_argument("--ndk", type=Path, help="NDK root (default: environment or pinned SDK installation)")
    ap.add_argument("--nm", default="arm-linux-gnueabi-nm", help="ARM-capable nm executable")
    ap.add_argument("--list", action="store_true", help="print the SDK identifiers used and exit")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    port = args.origin / "3ds_port"
    unit_objects = native_units(port)
    units = list(unit_objects)
    files = units + local_headers(port, units)
    used, called = sdk_identifiers(files)
    if args.list:
        print("\n".join(sorted(used)))
        return 0

    tokens, inline = header_index([args.android / "shim" / "include", args.android / "gpu" / "include"])
    own = self_declared(files)
    undeclared = sorted(n for n in used if n not in tokens and n not in own)

    lib = args.lib or args.tree / "3ds_port" / "emerald3ds.elf"
    objects = [args.tree / "3ds_port" / obj for obj in unit_objects.values()]
    missing_objects = [o for o in objects if not o.exists()]
    defined = nm(lib, "--defined-only", tool=args.nm) if lib.exists() and not args.headers_only else set()

    undefined: list[str] = []
    sdk_objects: list[Path] = []
    method = "not checked (--headers-only)" if args.headers_only else "not checked (library not built)"
    if not args.headers_only and lib.exists() and not missing_objects:
        method = "native objects' undefined symbols vs linked SDK objects, NDK exports and %s" % lib
        provided = defined | ndk_exports(find_ndk(args.ndk), args.nm)
        sdk_objects = linked_sdk_objects(args.tree)
        for obj in sdk_objects:
            provided |= nm(obj, "-g", "--defined-only", tool=args.nm)
        need = set()
        for o in objects:
            need |= nm(o, "-u", tool=args.nm)
        need = wrapped_references(args.tree, need)
        undefined = sorted(n for n in need if n not in provided)
    elif not args.headers_only and lib.exists():
        method = "call sites in the source vs %s (%d native objects not built)" % (lib, len(missing_objects))
        undefined = sorted(n for n in called if n not in inline and n not in defined and n not in own)

    report = {
        "native_units": [str(u.relative_to(args.origin)) for u in units],
        "sdk_identifiers": len(used),
        "undeclared": undeclared,
        "undefined": undefined,
        "undefined_method": method,
        "native_objects_missing": [str(o.relative_to(args.tree)) for o in missing_objects],
        "linked_sdk_objects": [str(o.relative_to(args.tree.resolve())) for o in sdk_objects],
        "headers_only": args.headers_only,
    }
    if args.json:
        print(json.dumps(report, indent=1))
    else:
        print("check_shim_coverage: %d native units, %d SDK identifiers"
              % (len(units), len(used)))
        print("  undeclared in android/shim/include + android/gpu/include: %d" % len(undeclared))
        for n in undeclared:
            print("    " + n)
        print("  undefined in the library [%s]: %d" % (method, len(undefined)))
        for n in undefined:
            print("    " + n)
        if missing_objects and not args.headers_only:
            print("  native objects not built: %s" % ", ".join(o.name for o in missing_objects))
    gaps = bool(undeclared or undefined or (not args.headers_only and (not lib.exists() or missing_objects)))
    return 1 if gaps else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"check_shim_coverage: {exc}", file=sys.stderr)
        sys.exit(2)
