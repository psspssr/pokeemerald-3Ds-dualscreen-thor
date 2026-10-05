#!/usr/bin/env python3
"""Test actual berry/feeding callbacks, windows and compositor geometry."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

from run_summary_tests import function
from run_starter_tests import declaration

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import PatchError, strict_apply

TARGETS = {
    "berry_tag_screen": ("static bool8 InitBerryTagScreen(void)", {"VblankCB"}),
    "use_pokeblock": ("static void ShowUsePokeblockMenu(void)", {"NULL", "VBlankCB_UsePokeblockMenu"}),
    "pokeblock_feed": ("static bool8 LoadPokeblockFeedScene(void)", {"VBlankCB_PokeblockFeed"}),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true")
    parser.add_argument("--target", choices=("all", *TARGETS), default="all")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="emerald-item-geometry-") as directory:
        work = Path(directory)
        mk = work / "3ds_port/full.mk"
        mk.parent.mkdir(parents=True)
        mk.write_bytes((TREE / "3ds_port/full.mk").read_bytes())
        patch = ROOT / "patches/android/087-item-menu-geometry.patch"
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
        text = mk.read_text()
        rules = text[text.index("CTR_GBA_CENTRED_SRCS :="):text.index("# Which centred screen")]
        video = (TREE / "3ds_port/src/3ds_video.c").read_text()
        start = video.index("    sStage = sStageRequested;")
        (work / "item_view.inc").write_text(video[start:video.index("    ClipToView();", start)])
        targets = TARGETS if args.target == "all" else (args.target,)
        for target in targets:
            source = (TREE / ("src/" + target + ".c")).read_text()
            entry, callbacks = TARGETS[target]
            # A translation-unit wrapper must never classify another scene's callback.
            assert set(re.findall(r"SetVBlankCallback\((\w+)\)", source)) == callbacks
            helper = "".join(e + "\n" for e in re.findall(r"enum\s*\{.*?\};", source, re.S) if "WIN_" in e)
            helper += declaration(source, "static const struct WindowTemplate sWindowTemplates[")
            yesno = target == "use_pokeblock"
            if yesno:
                helper += declaration(source, "static const struct WindowTemplate sUsePokeblockYesNoWinTemplate")
            (work / "item_windows.inc").write_text(helper)
            obj = "build/root/src/" + target + ".o"
            probe = work / "probe.mk"
            probe.write_text(rules + "\n" + obj + ":\n\t@printf '%s\\n' '$(FULLCFLAGS)'\n")
            flags = subprocess.run(["make", "--no-print-directory", "-s", "-B", "-f", str(probe),
                                    "ROOT=" + str(TREE), obj], cwd=TREE / "3ds_port",
                                   capture_output=True, text=True, check=True)
            flags = shlex.split(flags.stdout.strip())
            cc = os.environ.get("CC", "cc")
            callback = work / "callback.c"
            callback.write_text(function(source, entry))
            expanded = subprocess.run([cc, "-E", "-P", *flags, str(callback)],
                                      capture_output=True, text=True, check=True).stdout
            owned = next(c for c in callbacks if c != "NULL")
            centred = "CtrCentred_SetVBlankCallback(" + owned + ")" in expanded
            binary = work / target
            subprocess.run([cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            "-DPORTABLE", "-DMODERN=1", "-DPLATFORM_3DS", *flags,
                            "-DTEST_CALLBACK_CENTRED=" + str(int(centred)),
                            "-DTEST_HAS_YESNO=" + str(int(yesno)),
                            "-iquote" + str(TREE / "include"), "-iquote" + str(TREE / "3ds_port/include"),
                            "-I" + str(work), str(ROOT / "android/native/test/test_item_menu_geometry.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary), target], check=True, timeout=10,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
