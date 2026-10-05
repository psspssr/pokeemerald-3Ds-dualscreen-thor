#!/usr/bin/env python3
"""Exercise real Bag-grid to vertical-menu initialization and touch selection.

--before with a pristine generated --source reproduces the stale grid metadata.
The test never writes to the generated tree or a game save.
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
from bootstrap import strict_apply


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "build/upstream/src/menu.c")
    parser.add_argument("--before", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="emerald-menu-state-") as folder:
        work = Path(folder)
        path = work / "src/menu.c"
        path.parent.mkdir()
        path.write_bytes(args.source.read_bytes())
        if not args.before:
            strict_apply(work, ROOT / "patches/android/083-menu-grid-state.patch")
        source = path.read_text()
        declarations = re.search(r"struct Menu\n\{.*?\n\};", source, re.S)[0]
        (work / "menu_data.inc").write_text(declarations + "\nstatic struct Menu sMenu;\n")
        signatures = (
            "void RedrawMenuCursor(u8 oldPos, u8 newPos)",
            "u8 Menu_MoveCursor(s8 cursorDelta)",
            "u8 Menu_MoveCursorNoWrapAround(s8 cursorDelta)",
            "static void MoveMenuGridCursor(u8 oldCursorPos, u8 newCursorPos)",
            "u8 ChangeMenuGridCursorPosition(s8 deltaX, s8 deltaY)",
            "static u8 InitMenuGrid(u8 windowId, u8 fontId, u8 left, u8 top, u8 optionWidth, u8 optionHeight, u8 columns, u8 rows, u8 numChoices, u8 cursorPos)",
            "u8 InitMenuActionGrid(u8 windowId, u8 optionWidth, u8 columns, u8 rows, u8 initialCursorPos)",
            "static u8 InitMenu(u8 windowId, u8 fontId, u8 left, u8 top, u8 cursorHeight, u8 numChoices, u8 initialCursorPos, bool8 muteAPress)",
            "u8 InitMenuInUpperLeftCorner(u8 windowId, u8 itemCount, u8 initialCursorPos, bool8 APressMuted)",
            "s8 CtrMenu_EntryAt(s16 x, s16 y)",
            "s8 CtrMenu_Choose(s8 entry, bool8 sound)",
        )
        (work / "menu_helpers.inc").write_text("\n".join(function(source, x) for x in signatures))
        binary = work / "menu-state"
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I" + str(work),
            str(ROOT / "android/native/test/test_menu_state.c"), "-o", str(binary)
        ], check=True)
        subprocess.run([str(binary)], check=True, timeout=10,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
