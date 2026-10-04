#!/usr/bin/env python3
"""Sanitize actual Summary tilemap and input functions after Android overlays.

--source allows the same fixture to reproduce the unpatched asset-stub read.
No game save, Android device or generated source is modified.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def function(source, signature):
    start = source.index(signature + "\n{")
    begin = source.index("{", start)
    depth = 0
    for i in range(begin, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if not depth:
                return source[start:i + 1] + "\n"
    raise RuntimeError("unterminated function: " + signature)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "build/upstream/src/pokemon_summary_screen.c")
    parser.add_argument("--case", choices=("all", "sliding"), default="all")
    args = parser.parse_args()
    source = args.source.read_text()
    helpers = function(source, "static void ChangeTilemap(const struct TilemapCtrl *unkStruct, u16 *dest, u8 c, bool8 d)")
    helpers += function(source, "static void TilemapFiveMovesDisplay(u16 *dst, u16 palette, bool8 remove)")
    with tempfile.TemporaryDirectory(prefix="emerald-summary-test-") as folder:
        work = Path(folder)
        (work / "helpers.inc").write_text(helpers)
        for external in (True, False):
            binary = work / ("external" if external else "embedded")
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g",
                            "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                            "-fno-omit-frame-pointer", *( ["-DPORT_BRIDGE"] if external else []),
                            "-I" + str(work), str(ROOT / "android/native/test/test_summary_assets.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary), args.case], check=True, timeout=10,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
        if args.case == "all":
            if "CtrSummary_ReplaceTouch" not in source:
                raise RuntimeError("Updated upstream Summary touch handlers are missing; run bootstrap first")
            enum = re.search(r"enum\s*\{\s*CTR_TOUCH_NONE,.*?\};", source, re.S)[0]
            touch = "\n".join(re.findall(r"^#define CTR_MOVE_.*$", source, re.M)) + "\n" + enum + "\n"
            touch += "#define CTR_ANY_TAP() CtrSummary_AnyTap()\n"
            signatures = [
                "void CtrSummary_Tap(s16 x, s16 y)",
                "static bool8 CtrSummary_TakeTap(s16 *x, s16 *y)",
                "static bool8 CtrSummary_OnCancel(s16 x, s16 y)",
                "static s8 CtrSummary_MoveRowAt(s16 x, s16 y)",
                "static u8 CtrSummary_MoveTouch(s16 *data, u8 *indexPtr, u8 maxIndex)",
                "static bool8 CtrSummary_PageTouch(u8 taskId, s16 x, s16 y)",
                "static u8 CtrSummary_ReplaceTouch(u8 taskId, s16 *data)",
                "static bool8 CtrSummary_AnyTap(void)",
                "static bool8 CtrSummary_TouchInput(u8 taskId)",
                "static void ChangeSelectedMove(s16 *taskData, s8 direction, u8 *moveIndexPtr)",
                "static void SwitchToMoveSelection(u8 taskId)",
                "static void Task_HandleInput_MoveSelect(u8 taskId)",
                "static bool8 HasMoreThanOneMove(void)",
                "static void CloseMoveSelectMode(u8 taskId)",
                "static void SwitchToMovePositionSwitchMode(u8 taskId)",
                "static void Task_HandleInput_MovePositionSwitch(u8 taskId)",
                "static void ExitMovePositionSwitchMode(u8 taskId, bool8 swapMoves)",
                "static void Task_SetHandleReplaceMoveInput(u8 taskId)",
                "static void Task_HandleReplaceMoveInput(u8 taskId)",
                "static bool8 CanReplaceMove(void)",
                "static void ShowCantForgetHMsWindow(u8 taskId)",
                "static void Task_HandleInputCantForgetHMsMoves(u8 taskId)",
                "static void PutPageWindowTilemaps(u8 page)",
            ]
            touch += "\n".join(function(source, signature) for signature in signatures)
            (work / "touch.inc").write_text(touch)
            binary = work / "touch"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g", "-DPLATFORM_3DS",
                            "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                            "-fno-omit-frame-pointer", "-I" + str(work),
                            str(ROOT / "android/native/test/test_summary_touch.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
