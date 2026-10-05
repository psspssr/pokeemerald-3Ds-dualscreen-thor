#!/usr/bin/env python3
"""Sanitize real contest graphics reads and tested Pokeblock/contest geometry."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

from run_summary_tests import function

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import PatchError, strict_apply


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true")
    parser.add_argument("--case", choices=("all", "applause", "icons", "text", "geometry"), default="all")
    parser.add_argument("--target", choices=("all", "pokeblock", "contest", "contest_util"), default="all")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="emerald-contest-") as directory:
        work = Path(directory)
        for rel in ("3ds_port/full.mk", "src/contest.c", "src/contest_util.c"):
            p = work / rel; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes((TREE / rel).read_bytes())
        patch = ROOT / "patches/android/086-contest-pokeblock-graphics.patch"
        # Later patches extend the make list or add earlier lines in contest.c.
        # Normalize exact later hunks for these copied files, then restore them.
        selected = ["--include=" + rel for rel in ("3ds_port/full.mk", "src/contest.c", "src/contest_util.c")]
        later = []
        for candidate in sorted((ROOT / "patches/android").glob("*.patch"), reverse=True):
            if candidate.name <= patch.name:
                continue
            try:
                strict_apply(work, candidate, ["--reverse", *selected])
                later.append(candidate)
            except PatchError:
                pass
        try:
            strict_apply(work, patch, ["--reverse"])
        except PatchError:
            strict_apply(work, patch)
            strict_apply(work, patch, ["--reverse"])
        if not args.before:
            strict_apply(work, patch)
            for candidate in reversed(later):
                strict_apply(work, candidate, selected)
        contest = (work / "src/contest.c").read_text()
        results = (work / "src/contest_util.c").read_text()
        bios = (TREE / "src/platform/bios.c").read_text()
        helpers = ""
        for signature in ("static uint32_t CPUReadMemory(const void *src)",
                          "static void CPUWriteMemory(void *dest, uint32_t val)",
                          "static uint16_t CPUReadHalfWord(const void *src)",
                          "static void CPUWriteHalfWord(void *dest, uint16_t val)",
                          "void CpuSet(const void *src, void *dst, u32 cnt)"):
            helpers += function(bios, signature)
        helpers += function(contest, "static void UpdateApplauseMeter(void)")
        helpers += function(contest, "static void Task_ShowMoveSelectScreen(u8 taskId)")
        helpers += function(results, "static void LoadContestMonIcon(u16 species, u8 monIndex, u8 srcOffset, u8 useDmaNow, u32 personality)")
        helpers += function(results, "static s32 DrawResultsTextWindow(const u8 *text, u8 spriteId)")
        (work / "contest_helpers.inc").write_text(helpers)
        makefile = (work / "3ds_port/full.mk").read_text()
        rules = makefile[makefile.index("CTR_GBA_CENTRED_SRCS :="):makefile.index("# Which centred screen")]
        owned = {"pokeblock": {"VBlankCB_PokeblockMenu"}, "contest": {"NULL", "VBlankCB_Contest"},
                 "contest_util": {"NULL", "VBlankCB_ShowContestResults"}}
        targets = tuple(owned) if args.target == "all" else (args.target,)
        for target in targets:
            source = (TREE / ("src/" + target + ".c")).read_text()
            assert set(re.findall(r"SetVBlankCallback\((\w+)\)", source)) == owned[target]
            probe = work / "probe.mk"
            obj = "build/root/src/" + target + ".o"
            probe.write_text(rules + "\n" + obj + ":\n\t@printf '%s\\n' '$(FULLCFLAGS)'\n")
            flags = subprocess.run(["make", "--no-print-directory", "-s", "-B", "-f", str(probe),
                                    "ROOT=" + str(TREE), obj], cwd=TREE / "3ds_port",
                                   capture_output=True, text=True, check=True)
            for external in (True, False):
                binary = work / (target + ("-external" if external else "-embedded"))
                subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                                "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-DPORTABLE", "-DMODERN=1",
                                "-DPLATFORM_3DS", *(["-DPORT_BRIDGE"] if external else []),
                                *shlex.split(flags.stdout.strip()), "-iquote" + str(TREE / "include"), "-I" + str(work),
                                str(ROOT / "android/native/test/test_contest_graphics.c"), "-o", str(binary)], check=True)
                subprocess.run([str(binary), args.case, target], check=True, timeout=10,
                               env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
