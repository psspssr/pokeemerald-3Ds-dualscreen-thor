#!/usr/bin/env python3
"""Sanitize the actual PC initialization, screen dispatch and partial cleanup.

Only a temporary source copy is patched. No game save, Android device or
generated source is modified. --before reproduces the old failure paths.
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
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import PatchError, strict_apply


def declaration(source, name):
    return re.search(r"struct " + name + r"\n\{.*?\n\};", source, re.S)[0] + "\n"


def enum_containing(source, name):
    return next(match[0] for match in re.finditer(r"enum\s*\{.*?\};", source, re.S)
                if re.search(r"\b" + name + r"\b", match[0])) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true")
    parser.add_argument("--case", choices=("all", "window", "item-state", "cleanup", "normal", "dispatch", "allocation"), default="all")
    args = parser.parse_args()
    patch = ROOT / "patches/android/099-storage-init-cleanup.patch"
    with tempfile.TemporaryDirectory(prefix="emerald-storage-init-") as directory:
        work = Path(directory)
        target = work / "src/pokemon_storage_system.c"
        target.parent.mkdir(parents=True)
        target.write_bytes((TREE / "src/pokemon_storage_system.c").read_bytes())
        if patch.exists():
            try:
                strict_apply(work, patch, ["--reverse"])
            except PatchError:
                strict_apply(work, patch)
                strict_apply(work, patch, ["--reverse"])
            if not args.before:
                strict_apply(work, patch)
        elif not args.before:
            raise RuntimeError("storage cleanup overlay is missing")
        source = target.read_text()
        definitions = ""
        for name in ("OPTION_WITHDRAW", "SCREEN_CHANGE_EXIT_BOX", "CURSOR_AREA_IN_HAND",
                     "TILEMAPID_PKMN_DATA", "WIN_DISPLAY_INFO", "GFXTAG_MARKING_MENU", "PALTAG_MARKING_MENU"):
            # CURSOR_AREA_IN_HAND is the alias immediately following its enum.
            definitions += enum_containing(source, "CURSOR_AREA_IN_BOX" if name == "CURSOR_AREA_IN_HAND" else name)
        for name in ("MAX_MON_ICONS", "MAX_ITEM_ICONS", "CURSOR_AREA_IN_HAND"):
            definitions += re.search(r"^#define " + name + r"\b.*$", source, re.M)[0] + "\n"
        for name in ("StorageMenu", "UnkUtilData", "UnkUtil", "ChooseBoxMenu", "ItemIcon",
                     "PokemonStorageSystemData", "TilemapUtil_RectData", "TilemapUtil"):
            definitions += declaration(source, name)
        definitions += re.search(r"EWRAM_DATA static struct\n\{[^}]*\} \*sMultiMove = NULL;", source)[0] + "\n"
        definitions += re.search(r"static const struct WindowTemplate sWindowTemplate_MultiMove =\n\{.*?\n\};", source, re.S)[0] + "\n"
        (work / "storage_defs.inc").write_text(definitions)
        signatures = (
            "static void EnterPokeStorage(u8 boxOption)",
            "static void CB2_ReturnToPokeStorage(void)",
            "static void SetPokeStorageTask(TaskFunc newFunc)",
            "static void Task_InitPokeStorage(u8 taskId)",
            "static void Task_ChangeScreen(u8 taskId)",
            "static void FreePokeStorageData(void)",
            "static bool8 MultiMove_Init(void)",
            "static void MultiMove_Free(void)",
            "static bool8 IsMovingItem(void)",
            "static u16 GetMovingItemId(void)",
            "static void TilemapUtil_Init(u8 count)",
            "static void TilemapUtil_Free(void)",
        )
        (work / "storage_declarations.inc").write_text("\n".join(sig + ";" for sig in signatures))
        (work / "storage_helpers.inc").write_text("\n".join(function(source, sig) for sig in signatures))
        binary = work / "storage-init"
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-DPORTABLE",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(work), "-iquote", str(TREE / "include"),
                        str(ROOT / "android/native/test/test_storage_init.c"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary), args.case], check=True, timeout=20,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
