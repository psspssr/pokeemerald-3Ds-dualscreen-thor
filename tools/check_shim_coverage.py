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

When the native objects have been built, "undefined" is exact: every
undefined symbol of a native object that neither the library nor the NDK
libraries define. Before that, it is estimated from call sites in the source.
SDK identifiers are recognised by libctru/citro naming (C3D_*, gfx*, svc*,
KEY_*, ...); see SDK_PATTERN.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SDK_UPPER = (r"(?:C3D|C3Di|C2D|C2Di|Tex3DS|GPU|GPUCMD|GX|GSP|GSPGPU|DVLB|DVLE|DVLP|NDSP|APT|APTHOOK|"
             r"HID|KEY|GFX|MEMOP|MEMPERM|MEMSTATE|RESLIMIT|RESET|USERBREAK|SYSCLOCK|OS|FS|FSUSER|ARCHIVE|"
             r"CFG|CFGU|PTMU|MCUHWC|CUR|RL|RS|RM|RD|RC|Mtx|FVec3|FVec4|Quat|LightLock|LightEvent|"
             r"LightSemaphore|RecursiveLock|CondVar|ROMFS|Result)_\w*")
SDK_LOWER = (r"(?:gfx|gsp|gspgpu|hid|apt|ndsp|romfs|console|linear|vram|mappable|svc|os|thread|cfgu|"
             r"ptmu|mcuHwc|srv|psm|irrst|shader|shaderProgram|shaderInstance)[A-Z0-9]\w*")
SDK_NAMES = (r"(?:Handle|Result|Thread|PrintConsole|ndspWaveBuf|aptHookCookie|devoptab_t|devoptab_list|"
             r"R_FAILED|R_SUCCEEDED|MAKERESULT|BIT|u8|u16|u32|u64|s8|s16|s32|s64|vu8|vu16|vu32|vu64|vs32|"
             r"fake_heap_start|fake_heap_end|__ctru_\w+|__system_\w+|__C3D_\w+|Tex3DS\w*|C3D\w+|C2D\w+|"
             r"CTRU_\w+|DVLB_s|DVLE_s|DVLP_s|shaderProgram_s|shaderInstance_s)")
SDK_PATTERN = re.compile(r"^(?:%s|%s|%s)$" % (SDK_UPPER, SDK_LOWER, SDK_NAMES))
IDENT = re.compile(r"\b[A-Za-z_]\w*\b")


def strip_c(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    text = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)
    return re.sub(r"'(?:\\.|[^'\\\n])*'", "''", text)


def native_units(port: Path) -> dict[Path, str]:
    """The native translation units and their objects (relative to 3ds_port),
    read from origin's makefiles."""
    units: dict[str, str] = {}
    for line in (port / "Makefile").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^SOURCES\s*[:+]?=\s*(.*)$", line)
        if m:
            for src in m.group(1).split():
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
    return {port / src: obj for src, obj in units.items()}


def local_headers(port: Path, units: list[Path]) -> list[Path]:
    """Origin headers the native units include that use the SDK themselves."""
    found: list[Path] = []
    for unit in units:
        for name in re.findall(r'#\s*include\s+"([^"]+)"', unit.read_text(encoding="utf-8", errors="ignore")):
            for base in (unit.parent, port / "include"):
                path = base / name
                if path.exists() and path not in found:
                    text = path.read_text(encoding="utf-8", errors="ignore")
                    if re.search(r"#\s*include\s+<(3ds|citro[23]d|tex3ds)", text):
                        found.append(path)
    return found


def sdk_identifiers(files: list[Path]) -> tuple[set[str], set[str]]:
    used: set[str] = set()
    called: set[str] = set()
    for path in files:
        text = strip_c(path.read_text(encoding="utf-8", errors="ignore"))
        for name in IDENT.findall(text):
            if SDK_PATTERN.match(name):
                used.add(name)
        for name in re.findall(r"\b([A-Za-z_]\w*)\s*\(", text):
            if SDK_PATTERN.match(name):
                called.add(name)
    return used, called


def self_declared(files: list[Path]) -> set[str]:
    """Names origin declares itself at file scope (`extern u32 __ctru_heap;`,
    its own __system_allocateHeaps): they need a definition, not a header."""
    names: set[str] = set()
    for path in files:
        for line in strip_c(path.read_text(encoding="utf-8", errors="ignore")).splitlines():
            if not line or line[0] in " \t#{}":
                continue
            names.update(re.findall(r"\b([A-Za-z_]\w*)\s*(?=[\[=;,(])", line))
    return names


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


def nm(path: Path, *flags: str) -> set[str]:
    out = subprocess.run(["arm-linux-gnueabi-nm", *flags, str(path)], capture_output=True, text=True,
                         errors="ignore")
    if out.returncode != 0:
        return set()
    return {line.split()[-1].split("@")[0] for line in out.stdout.splitlines() if line.split()}


def ndk_exports(tree: Path) -> set[str]:
    import glob
    names: set[str] = set()
    for lib in glob.glob(str(Path.home() / "Android/Sdk/ndk/*/toolchains/llvm/prebuilt/linux-x86_64/sysroot/"
                             "usr/lib/arm-linux-androideabi/28/*.so")):
        names |= nm(Path(lib), "-D", "--defined-only")
    return names


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--origin", type=Path, default=ROOT / "origin")
    ap.add_argument("--android", type=Path, default=ROOT / "android")
    ap.add_argument("--tree", type=Path, default=ROOT / "build" / "upstream",
                    help="the bootstrapped tree, for the built objects and library")
    ap.add_argument("--lib", type=Path, default=None,
                    help="the unstripped library (default: <tree>/3ds_port/emerald3ds.elf)")
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
    defined = nm(lib, "--defined-only") if lib.exists() else set()

    undefined: list[str] = []
    method = "none (library not built)"
    if lib.exists() and not missing_objects:
        method = "native objects' undefined symbols vs %s" % lib
        provided = defined | ndk_exports(args.tree)
        need = set()
        for o in objects:
            need |= nm(o, "-u")
        undefined = sorted(n for n in need if n not in provided)
    elif lib.exists():
        method = "call sites in the source vs %s (%d native objects not built)" % (lib, len(missing_objects))
        undefined = sorted(n for n in called if n not in inline and n not in defined)

    report = {
        "native_units": [str(u.relative_to(args.origin)) for u in units],
        "sdk_identifiers": len(used),
        "undeclared": undeclared,
        "undefined": undefined,
        "undefined_method": method,
        "native_objects_missing": [str(o.relative_to(args.tree)) for o in missing_objects],
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
        if missing_objects:
            print("  native objects not built: %s" % ", ".join(o.name for o in missing_objects))
    gaps = bool(undeclared or undefined or not lib.exists() or missing_objects)
    return 1 if gaps else 0


if __name__ == "__main__":
    sys.exit(main())
