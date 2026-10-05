#!/usr/bin/env python3
"""Prove hidden Contest hearts stay out of the centred native canvas."""
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
from bootstrap import PatchError, strict_apply


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="emerald-contest-hearts-") as directory:
        work = Path(directory)
        src = work / "src/contest.c"
        src.parent.mkdir()
        src.write_bytes((TREE / "src/contest.c").read_bytes())
        patch = ROOT / "patches/android/095-contest-hidden-hearts.patch"
        try:
            strict_apply(work, patch, ["--reverse"])
        except PatchError:
            strict_apply(work, patch)
            strict_apply(work, patch, ["--reverse"])
        if not args.before:
            strict_apply(work, patch)
        contest = src.read_text()
        sprites = (TREE / "src/sprite.c").read_text()
        video = (TREE / "3ds_port/src/3ds_video.c").read_text()
        helpers = declaration(contest, "static const struct OamData sOam_SliderHeart")
        helpers += declaration(contest, "static const u8 sSliderHeartYPositions[")
        helpers += declaration(sprites, "static const u8 sCenterToCornerVecTable[")
        helpers += function(sprites, "void CalcCenterToCornerVec(struct Sprite *sprite, u8 shape, u8 size, u8 affineMode)")
        helpers += function(sprites, "void UpdateOamCoords(void)")
        helpers += function(contest, "static void SetBottomSliderHeartsInvisibility(bool8 invisible)")
        helpers += function(contest, "static void UpdateSliderHeartSpriteYPositions(void)")
        helpers += "\n".join(re.findall(r"^#define s(?:Contestant|TargetX|MoveX)\s+.*$", contest, re.M)) + "\n"
        helpers += function(contest, "static void UpdateHeartSlider(u8 contestant)")
        helpers += function(contest, "static void SpriteCB_UpdateHeartSlider(struct Sprite *sprite)")
        (work / "heart_helpers.inc").write_text(helpers)
        clip = "\n".join(re.findall(r"^#define VIEW_(?:LEFT|RIGHT|TOP|BOTTOM) .*$", video, re.M)) + "\n"
        clip += function(video, "static void ClipToView(void)")
        (work / "heart_clip.inc").write_text(clip)
        # The actual OAM decode and centred wrapping precede the renderer's
        # clip test. Do not replace them with a guessed signed-coordinate model.
        start = video.index("        int x = attr1 & 511, y = attr0 & 255;")
        end = video.index("        else\n", start)
        (work / "heart_decode.inc").write_text(video[start:end])
        for native in (True, False):
            binary = work / ("native" if native else "gba")
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-DPORTABLE", "-DMODERN=1",
                            *(["-DPORT_BRIDGE"] if native else []), "-I" + str(work),
                            "-iquote" + str(TREE / "include"), "-iquote" + str(TREE / "3ds_port/include"),
                            str(ROOT / "android/native/test/test_contest_hearts.c"), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
