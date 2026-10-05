#!/usr/bin/env python3
"""Sanitize real condition-icon descriptors and all six map-popup outlines."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from run_summary_tests import function
from run_starter_tests import declaration

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import PatchError, strict_apply, patch_files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true")
    parser.add_argument("--case", choices=("all", "condition", "popup"), default="all")
    parser.add_argument("--theme", type=int, choices=range(6))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="emerald-menu-assets-") as directory:
        work = Path(directory)
        for name in ("092-condition-icon-palette.patch", "093-map-popup-outline-bound.patch"):
            patch = ROOT / "patches/android" / name
            for rel in patch_files(patch):
                p = work / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes((TREE / rel).read_bytes())
            try:
                strict_apply(work, patch, ["--reverse"])
            except PatchError:
                strict_apply(work, patch)
                strict_apply(work, patch, ["--reverse"])
            if not args.before:
                strict_apply(work, patch)
        condition = (work / "src/menu_specialized.c").read_text()
        popup = (work / "src/map_name_popup.c").read_text()
        sprite = (TREE / "src/sprite.c").read_text()
        dma = (TREE / "src/dma3_manager.c").read_text()
        bios = (TREE / "src/platform/bios.c").read_text()
        tags = (TREE / "include/menu_specialized.h").read_text()
        helpers = next(e for e in re.findall(r"enum\s*\{.*?\};", tags, re.S) if "TAG_CONDITION_MON =" in e) + "\n"
        helpers += function(condition, "void LoadConditionSelectionIcons(struct SpriteSheet *sheets, struct SpriteTemplate *template, struct SpritePalette *pals)")
        helpers += function(sprite, "void DoLoadSpritePalette(const u16 *src, u16 paletteOffset)")
        helpers += declaration(dma, "struct Dma3Request\n")
        helpers += "static struct Dma3Request sDma3Requests[128];\nstatic vbool8 sDma3ManagerLocked;\nstatic u8 sDma3RequestCursor;\n"
        helpers += function(dma, "s16 RequestDma3Copy(const void *src, void *dest, u16 size, u8 mode)")
        (work / "menu_helpers.inc").write_text(helpers)
        popup_helpers = "\n".join(re.findall(r"^#define TILE_.*$", popup, re.M)) + "\n"
        popup_helpers += function(popup, "static void DrawMapNamePopUpFrame(u8 bg, u8 x, u8 y, u8 deltaX, u8 deltaY, u8 unused)")
        popup_helpers += function(popup, "static void LoadMapNamePopUpWindowBg(void)")
        (work / "popup_helpers.inc").write_text(popup_helpers)
        (work / "menu_bios.inc").write_text("\n".join(function(bios, signature) for signature in (
            "static uint32_t CPUReadMemory(const void *src)", "static void CPUWriteMemory(void *dest, uint32_t val)",
            "static uint16_t CPUReadHalfWord(const void *src)", "static void CPUWriteHalfWord(void *dest, uint16_t val)",
            "void CpuSet(const void *src, void *dst, u32 cnt)")))
        for external in (True, False):
            binary = work / ("external" if external else "embedded")
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-array-bounds", "-Wno-unused-parameter", "-fsanitize=address,undefined",
                            "-fno-omit-frame-pointer", "-DPORTABLE", "-DMODERN=1",
                            *(["-DPORT_BRIDGE"] if external else []), "-I" + str(work),
                            "-iquote" + str(TREE / "include"),
                            str(ROOT / "android/native/test/test_menu_assets.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary), args.case, str(args.theme if args.theme is not None else -1)],
                           check=True, timeout=10, env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
