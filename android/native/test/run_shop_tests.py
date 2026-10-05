#!/usr/bin/env python3
"""Check real shop metatile reads and the shop target's centred GBA geometry.

Both pristine and already patched generated input are accepted by exact084
forward/reverse checks. --before proves each defect independently with --case.
"""
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
    parser.add_argument("--case", choices=("all", "assets", "geometry"), default="all")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="emerald-shop-") as directory:
        work = Path(directory)
        for rel in ("src/shop.c", "3ds_port/full.mk"):
            target = work / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((TREE / rel).read_bytes())
        patch = ROOT / "patches/android/084-shop-screen.patch"
        # Later focused scene patches extend the same make source list. Undo
        # only exact later full.mk hunks before normalizing084, then restore
        # those hunks for the final-target test. Never touch the real tree.
        later = []
        for candidate in sorted((ROOT / "patches/android").glob("*.patch"), reverse=True):
            if candidate.name <= patch.name:
                continue
            try:
                strict_apply(work, candidate, ["--reverse", "--include=3ds_port/full.mk"])
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
                strict_apply(work, candidate, ["--include=3ds_port/full.mk"])
        source = (work / "src/shop.c").read_text()
        assert re.findall(r"SetVBlankCallback\((\w+)\)", source) == ["VBlankCB_BuyMenu"]
        signatures = (
            "static void BuyMenuDrawMapMetatileLayer(u16 *dest, s16 offset1, s16 offset2, const u16 *src)",
            "static void BuyMenuDrawMapMetatile(s16 x, s16 y, const u16 *src, u8 metatileLayerType)",
            "static bool8 BuyMenuCheckForOverlapWithMenuBg(int x, int y)",
            "static void BuyMenuDrawMapBg(void)",
        )
        (work / "shop_helpers.inc").write_text("\n".join(function(source, s) for s in signatures))
        makefile = (work / "3ds_port/full.mk").read_text()
        rules = makefile[makefile.index("CTR_GBA_CENTRED_SRCS :="):makefile.index("# Which centred screen")]
        probe = work / "probe.mk"
        probe.write_text(rules + "\nbuild/root/src/shop.o:\n\t@printf '%s\\n' '$(FULLCFLAGS)'\n")
        result = subprocess.run(["make", "--no-print-directory", "-s", "-B", "-f", str(probe),
                                 "ROOT=" + str(TREE), "build/root/src/shop.o"],
                                cwd=TREE / "3ds_port", capture_output=True, text=True, check=True)
        target_flags = shlex.split(result.stdout.strip())
        for external in (True, False):
            binary = work / ("external" if external else "embedded")
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-DPORTABLE", "-DMODERN=1",
                "-DPLATFORM_3DS", *(["-DPORT_BRIDGE"] if external else []), *target_flags,
                "-iquote" + str(TREE / "include"), "-I" + str(work),
                str(ROOT / "android/native/test/test_shop.c"), "-o", str(binary)
            ], check=True)
            subprocess.run([str(binary), args.case], check=True, timeout=10,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
