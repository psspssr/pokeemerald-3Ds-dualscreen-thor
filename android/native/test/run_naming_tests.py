#!/usr/bin/env python3
"""Sanitize actual naming touch/input handlers after Android overlays."""
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
from bootstrap import PatchError, strict_apply


def declaration(source, pattern):
    return re.search(pattern, source, re.S)[0] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=TREE / "src/naming_screen.c")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="emerald-naming-") as folder:
        work = Path(folder)
        path = work / "src/naming_screen.c"
        path.parent.mkdir()
        path.write_bytes(args.source.read_bytes())
        patch = ROOT / "patches/android/085-naming-touch-screen.patch"
        include = ["--include=src/naming_screen.c"]
        try:
            strict_apply(work, patch, ["--reverse", *include])
        except PatchError:
            strict_apply(work, patch, include)
            strict_apply(work, patch, ["--reverse", *include])
        strict_apply(work, patch, include)
        source = path.read_text()
        data = "\n".join(re.findall(r"^#define KB(?:ROW|COL)_COUNT .*", source, re.M)) + "\n"
        for first in ("INPUT_NONE", "WIN_KB_PAGE_1", "KBPAGE_SYMBOLS", "KEYBOARD_LETTERS_LOWER",
                      "KEY_ROLE_CHAR", "BUTTON_PAGE", "STATE_FADE_IN", "INPUT_STATE_DISABLED"):
            data += declaration(source, r"enum\s*\{\s*" + first + r"\b.*?\};")
        data += declaration((TREE / "include/naming_screen.h").read_text(), r"enum\s*\{\s*NAMING_SCREEN_PLAYER\b.*?\};")
        for name in ("NamingScreenTemplate", "NamingScreenData"):
            data += declaration(source, r"struct " + name + r"\s*\{.*?\};")
        for name in ("sPageColumnCounts", "sPageColumnXPos", "sPageToKeyboardId", "sButtonKeyRoles"):
            data += declaration(source, r"static const u8 " + name + r"\[.*?\};")
        data += declaration(source, r"static struct\s*\{\s*bool8 pending;.*?\} sCtrNamingTap;")
        (work / "naming_data.inc").write_text(data)
        signatures = (
            "bool8 CtrNaming_IsOpen(void)", "static bool8 CtrNaming_AcceptsTap(void)",
            "void CtrNaming_Tap(s16 x, s16 y)", "static bool8 CtrNaming_TakeTap(s16 *x, s16 *y)",
            "static bool8 CtrNaming_Acknowledge(void)", "static bool8 CtrNaming_TouchInput(struct Task *task)",
            "void DoNamingScreen(u8 templateNum, u8 *destBuffer, u16 monSpecies, u16 monGender, u32 monPersonality, MainCallback returnCallback)",
            "static u8 CurrentPageToKeyboardId(void)", "static u8 GetCurrentPageColumnCount(void)",
            "static u8 GetKeyRoleAtCursorPos(void)", "static u8 GetInputEvent(void)",
            "static void SetInputState(u8 state)", "static void MoveCursorToOKButton(void)",
            "static void Input_Disabled(struct Task *task)", "static void Input_Enabled(struct Task *task)",
            "static void Input_Override(struct Task *task)", "static void Task_HandleInput(u8 taskId)",
            "static bool8 KeyboardKeyHandler_Character(u8 input)", "static bool8 KeyboardKeyHandler_Page(u8 input)",
            "static bool8 KeyboardKeyHandler_Backspace(u8 input)", "static bool8 KeyboardKeyHandler_OK(u8 input)",
            "static bool8 SwapKeyboardPage(void)", "static bool8 HandleKeyboardEvent(void)",
            "static bool8 MainState_WaitSentToPCMessage(void)",
        )
        helpers = "#define tState data[0]\n#define tKeyboardEvent data[1]\n#define tButtonId data[2]\n"
        helpers += "\n".join(s + ";" for s in signatures) + "\n"
        helpers += declaration(source, r"static void \(\*const sInputFuncs\[\]\)\(struct Task \*\) =.*?\};")
        helpers += declaration(source, r"static bool8 \(\*const sKeyboardKeyHandlers\[\]\)\(u8\) =.*?\};")
        helpers += "\n".join(function(source, s) for s in signatures)
        (work / "naming_helpers.inc").write_text(helpers)
        binary = work / "naming"
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-DPLATFORM_3DS",
                        "-I" + str(work), str(ROOT / "android/native/test/test_naming_touch.c"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
