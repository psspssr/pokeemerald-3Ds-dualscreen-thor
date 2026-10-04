#!/usr/bin/env python3
"""Sanitize starter geometry using real game helpers and make target flags.

Run bootstrap first. --full-makefile permits a before/after build-flag probe
without changing the imported or generated sources.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from run_summary_tests import function

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()


def declaration(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n};", start) + 3] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full-makefile", type=Path, default=TREE / "3ds_port/full.mk")
    args = parser.parse_args()
    starter = (TREE / "src/starter_choose.c").read_text()
    video = (TREE / "3ds_port/src/3ds_video.c").read_text()
    makefile = args.full_makefile.read_text()
    rules = makefile[makefile.index("CTR_GBA_CENTRED_SRCS :="):makefile.index("# The PokéNav:")]
    helpers = "\n".join(re.findall(r"^#define (?:STARTER_MON_COUNT|STARTER_PKMN_POS_[XY]|tStarterSelection|tCircleSpriteId)\s+.*$", starter, re.M)) + "\n"
    for signature in (
        "static const struct WindowTemplate sWindowTemplates[]",
        "static const struct WindowTemplate sWindowTemplate_ConfirmStarter",
        "static const struct WindowTemplate sWindowTemplate_StarterLabel",
        "static const u8 sPokeballCoords[STARTER_MON_COUNT][2]",
        "static const u8 sStarterLabelCoords[STARTER_MON_COUNT][2]",
        "static const u8 sCursorCoords[][2]",
    ):
        helpers += declaration(starter, signature)
    for signature in (
        "static void SpriteCB_SelectionHand(struct Sprite *sprite)",
        "static void SpriteCB_StarterPokemon(struct Sprite *sprite)",
        "static void Task_WaitForStarterSprite(u8 taskId)",
    ):
        helpers += function(starter, signature)
    helpers += "\n".join(re.findall(r"^#define VIEW_(?:LEFT|RIGHT|TOP|BOTTOM) .*$", video, re.M)) + "\n"
    for signature in (
        "static int WindowEdge(unsigned limits, bool last, unsigned extent)",
        "static bool BattleCurtain(void)",
        "static void WindowSpan(unsigned limits, bool vertical, int *first, int *last)",
    ):
        helpers += function(video, signature)
    fill_end = video.index("} CentredFill;") + len("} CentredFill;")
    helpers += video[video.rfind("typedef struct", 0, fill_end):fill_end] + "\n"
    helpers += declaration(video, "static const CentredFill sCentredFills[CTR_CENTRED_SCREENS]")
    start = video.index("    sStage = sStageRequested;")
    view = video[start:video.index("    ClipToView();", start)]
    with tempfile.TemporaryDirectory(prefix="emerald-starter-geometry-") as directory:
        work = Path(directory)
        (work / "starter.inc").write_text(helpers)
        (work / "view.inc").write_text(view)
        probe = work / "probe.mk"
        probe.write_text(rules + "\nbuild/root/src/starter_choose.o:\n"
                         "\t@printf '%s\\n' '$(FULLCFLAGS)'\n")
        result = subprocess.run(["make", "--no-print-directory", "-s", "-B", "-f", str(probe),
                                 "ROOT=" + str(TREE), "build/root/src/starter_choose.o"],
                                cwd=TREE / "3ds_port", check=True, capture_output=True, text=True)
        target_flags = shlex.split(result.stdout.strip())
        print("Starter target flags: " + shlex.join(target_flags), flush=True)
        compiler = os.environ.get("CC", "cc")
        # Preprocess the actual initialization function, so the flag check also
        # verifies that its VBlank registration is routed to the starter view.
        callback = work / "callback.c"
        callback.write_text(function(starter, "void CB2_ChooseStarter(void)"))
        expanded = subprocess.run([compiler, "-E", "-P", *target_flags, str(callback)],
                                  check=True, capture_output=True, text=True).stdout
        centred = "CtrCentredStarter_SetVBlankCallback(VblankCB_StarterChoose)" in expanded
        binary = work / "starter"
        subprocess.run([compiler, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-D_GNU_SOURCE", "-DPORTABLE", "-DMODERN=1", "-DPLATFORM_3DS",
                        "-DTEST_STARTER_CALLBACK_CENTRED=" + str(int(centred)),
                        *target_flags, "-iquote" + str(TREE / "include"),
                        "-iquote" + str(TREE / "3ds_port/compat"),
                        "-iquote" + str(TREE / "3ds_port/include"), "-iquote" + str(work),
                        str(ROOT / "android/native/test/test_starter_alignment.c"), "-lm",
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
