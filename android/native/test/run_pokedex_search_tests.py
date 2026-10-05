#!/usr/bin/env python3
"""Match real Hoenn/National search drawings to their actual touch handlers.

--before reproduces the visible Hoenn OK failure. Both pristine and already
patched source are validated in an isolated copy; no generated tree is changed.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from run_summary_tests import function

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import PatchError, strict_apply


def declaration(source, name):
    match = re.search(r"^static const [^\n]+\b" + re.escape(name) + r"\[", source, re.M)
    if match is None:
        raise ValueError("Missing real declaration: " + name)
    end = source.index("\n};", match.start()) + 3
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    tree = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream"))
    parser.add_argument("--source", type=Path, default=tree / "src/pokedex.c")
    parser.add_argument("--before", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="emerald-dex-search-") as folder:
        work = Path(folder)
        path = work / "src/pokedex.c"
        path.parent.mkdir()
        path.write_bytes(args.source.read_bytes())
        patch = ROOT / "patches/android/097-pokedex-search-hitboxes.patch"
        try:
            strict_apply(work, patch, ["--reverse"])
        except PatchError:
            strict_apply(work, patch)
            strict_apply(work, patch, ["--reverse"])
        if not args.before:
            strict_apply(work, patch)
        source = path.read_text()
        data = []
        for first in ("SEARCH_NAME", "SEARCH_TOPBAR_SEARCH", "CTR_DEX_NONE"):
            data.append(re.search(r"enum\s*\{\s*" + first + r",.*?\};", source, re.S)[0])
        for name in ("SearchMenuItem", "SearchMenuTopBarItem"):
            data.append(re.search(r"struct " + name + r"\n\{.*?\n\};", source, re.S)[0])
        for name in ("sSearchMenuItems", "sSearchMenuTopBarItems",
                     "sSearchMovementMap_SearchHoennDex", "sSearchMovementMap_SearchNatDex",
                     "sSearchMovementMap_ShiftHoennDex", "sSearchMovementMap_ShiftNatDex"):
            data.append(declaration(source, name))
        declarations = "\n".join(data)
        text_stubs = "\n".join("static const u8 " + name + "[] = {0};"
                               for name in sorted(set(re.findall(r"\bgText_\w+", declarations))))
        macros = "\n".join(re.findall(r"^#define (?:SEARCH_BG_\w+|tTopBarItem|tMenuItem)\s+.*$", source, re.M))
        (work / "dex_search_data.inc").write_text(text_stubs + "\n" + declarations + "\n" + macros + "\n")
        signatures = (
            "static void DrawSearchMenuItemBgHighlight(u8 searchBg, bool8 unselected, bool8 disabled)",
            "static s8 CtrDex_SearchBarAt(s16 x, s16 y)",
            "static s8 CtrDex_SearchItemAt(s16 x, s16 y)",
            "static bool8 CtrDex_SearchHasItem(u8 topBarItem, u8 item)",
            "static u8 CtrDex_SearchTopBarAction(u8 taskId)",
            "static u8 CtrDex_SearchMenuAction(u8 taskId)",
        )
        (work / "dex_search_helpers.inc").write_text("\n".join(function(source, x) for x in signatures))
        binary = work / "dex-search"
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wno-implicit-fallthrough",  # upstream's commented title -> selection rectangle fallthrough
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I" + str(work),
            str(ROOT / "android/native/test/test_pokedex_search.c"), "-o", str(binary)
        ], check=True)
        subprocess.run([str(binary)], check=True, timeout=10,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
