#!/usr/bin/env python3
"""Exercise real graphics consumers with external stubs and embedded assets."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from run_summary_tests import function

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import PatchError, strict_apply, patch_files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true")
    parser.add_argument("--case", choices=("all", "evolution", "spotlight", "spiral", "grid", "reels", "contest"), default="all")
    args = parser.parse_args()
    patch = ROOT / "patches/android/088-external-graphics-offsets.patch"
    with tempfile.TemporaryDirectory(prefix="emerald-asset-offsets-") as directory:
        work = Path(directory)
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
        signatures = {
            "evolution_scene": ["static void InitMovingBgPalette(u16 *palette)"],
            "field_effect": ["u8 FldEff_RayquazaSpotlight(void)"],
            "battle_transition": ["static bool8 RectangularSpiral_Init(struct Task *task)",
                                  "static bool8 GridSquares_Main(struct Task *task)"],
            "slot_machine": ["static void LoadReelTimeWindowTilemap(s16 a0, s16 a1)"],
            "contest": ["static const u8 *GetTurnOrderNumberGfx(u8 contestant)"],
        }
        helpers = ""
        evolution = (work / "src/evolution_scene.c").read_text()
        helpers += re.search(r"static const u8 sBgAnim_PalIndexes\[\]\[16\] = \{.*?\};", evolution, re.S)[0] + "\n"
        for name, selected in signatures.items():
            source = (work / ("src/" + name + ".c")).read_text()
            helpers += "\n".join(function(source, signature) for signature in selected)
        (work / "asset_helpers.inc").write_text(helpers)
        bios = (TREE / "src/platform/bios.c").read_text()
        (work / "asset_bios.inc").write_text("\n".join(function(bios, signature) for signature in (
            "static uint32_t CPUReadMemory(const void *src)", "static void CPUWriteMemory(void *dest, uint32_t val)",
            "static uint16_t CPUReadHalfWord(const void *src)", "static void CPUWriteHalfWord(void *dest, uint16_t val)",
            "void CpuSet(const void *src, void *dst, u32 cnt)")))
        for external in (True, False):
            binary = work / ("external" if external else "embedded")
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-array-bounds", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            *(["-DPORT_BRIDGE"] if external else []), "-I" + str(work),
                            "-iquote" + str(TREE / "include"),
                            str(ROOT / "android/native/test/test_asset_offsets.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary), args.case], check=True, timeout=10,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
