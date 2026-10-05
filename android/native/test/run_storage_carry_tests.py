#!/usr/bin/env python3
"""Test PC carry rollback using actual handlers and synthetic, real encrypted mons.

The generated source is read only. Source extraction/overlays and all test state
live in a temporary directory; no player save or Android device is used.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from run_summary_tests import function
from run_storage_init_tests import declaration, enum_containing

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import PatchError, strict_apply


def replace_once(source, old, new):
    assert source.count(old) == 1, old
    return source.replace(old, new)


def check_notice_bounds(notice):
    menu = (TREE / "src/menu.c").read_text()
    text = (TREE / "src/text.c").read_text()
    window = re.search(r"sStandardTextBox_WindowTemplates\[\].*?\.width = (\d+),\s*\.height = (\d+)", menu, re.S)
    printer = function(menu, "u16 AddTextPrinterParameterized2(u8 windowId, u8 fontId, const u8 *str, u8 speed, void (*callback)(struct TextPrinterTemplate *, u16), u8 fgColor, u8 bgColor, u8 shadowColor)")
    assert "printer.letterSpacing = 0;" in printer and "printer.lineSpacing = 0;" in printer
    y = int(re.search(r"printer.y = (\d+)", printer)[1])
    step = int(re.search(r"\[FONT_NORMAL\].*?\.maxLetterHeight = (\d+)", text, re.S)[1])
    glyph_height = {int(v) for v in re.findall(r"gCurGlyph.height = (\d+)", function(text, "static void DecompressGlyph_Normal(u16 glyphId, bool32 isJapanese)"))}
    assert len(glyph_height) == 1
    widths = [int(v) for v in re.findall(r"\d+", re.search(r"gFontNormalLatinGlyphWidths\[\] = \{(.*?)\};", (TREE / "src/fonts.c").read_text(), re.S)[1])]
    notices = re.findall(r"(sText_Storage\w+)\[\]\s*=\s*\{(.*?)\}", notice, re.S)
    assert len(notices) == 2
    for name, body in notices:
        codes = [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|\d+", body)]
        assert codes[-1] == 255 and codes.count(254) == 1
        rows = bytes(codes[:-1]).split(bytes([254]))
        row_widths = [sum(widths[code] for code in row) for row in rows]
        assert max(row_widths) <= int(window[1]) * 8
        assert y + step + next(iter(glyph_height)) <= int(window[2]) * 8
        print("PASS", name, "uses", row_widths, "pixels within the real 216x32 dialogue window")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true")
    parser.add_argument("--patch", type=Path, default=ROOT / "patches/android/100-storage-carry-recovery.patch")
    parser.add_argument("--case", choices=("all", "party", "mail", "items", "commits", "notice"), default="all")
    args = parser.parse_args()
    cc = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory(prefix="emerald-storage-carry-") as folder:
        work = Path(folder)
        target = work / "src/pokemon_storage_system.c"
        target.parent.mkdir(parents=True)
        target.write_bytes((TREE / "src/pokemon_storage_system.c").read_bytes())
        if "CtrStorageCarry_Begin" in target.read_text():
            strict_apply(work, args.patch, ["--reverse"])
        if "sStorage = AllocZeroed(sizeof(*sStorage));" not in target.read_text():
            strict_apply(work, ROOT / "patches/android/099-storage-init-cleanup.patch")
        if not args.before:
            strict_apply(work, args.patch)
        source = target.read_text()
        definitions = ""
        for name in ("OPTION_WITHDRAW", "SCREEN_CHANGE_EXIT_BOX", "CURSOR_AREA_IN_BOX", "TILEMAPID_PKMN_DATA",
                     "WIN_DISPLAY_INFO", "GFXTAG_MARKING_MENU", "PALTAG_MARKING_MENU", "MODE_PARTY",
                     "MOVE_MODE_NORMAL", "CURSOR_ANIM_STILL", "ITEM_ANIM_PICK_UP", "ITEM_CB_TO_HAND",
                     "STATE_LOAD", "MSG_PUT_IN_BAG", "MENU_SUMMARY"):
            definitions += enum_containing(source, name)
        for name in ("MAX_MON_ICONS", "MAX_ITEM_ICONS", "CURSOR_AREA_IN_HAND"):
            definitions += re.search(r"^#define " + name + r"\b.*$", source, re.M)[0] + "\n"
        for name in ("StorageMenu", "UnkUtilData", "UnkUtil", "ChooseBoxMenu", "ItemIcon",
                     "PokemonStorageSystemData", "TilemapUtil_RectData", "TilemapUtil"):
            definitions += declaration(source, name)
        definitions += re.search(r"EWRAM_DATA static struct\n\{[^}]*\} \*sMultiMove = NULL;", source)[0] + "\n"
        definitions += re.search(r"static const struct WindowTemplate sWindowTemplate_MultiMove =\n\{.*?\n\};", source, re.S)[0] + "\n"
        helpers = ""
        if not args.before:
            definitions += declaration(source, "StorageCarryCheckpoint")
            definitions += "static struct StorageCarryCheckpoint sStorageCarry;\nstatic bool8 sStorageCarryRecovered, sStorageInitFailed;\n"
            # Use the real game preprocessor for the recovery notice.
            text = "\n".join(re.findall(r'static const u8 sText_Storage(?:CarryRecovered|MemoryFull)\[\] = _\(".*?"\);', source))
            assert text.count("static const") == 2
            notice = subprocess.run([str(TREE / "tools/preproc/preproc"), "-i", "storage_notice.c", "charmap.txt"],
                                    cwd=TREE, input=text.encode(), capture_output=True, check=True).stdout.decode()
            check_notice_bounds(notice)
            definitions += notice + "\n"
            for signature in ("static void CtrStorageCarry_Begin(void)", "static void CtrStorageCarry_Commit(void)",
                              "static void CtrStorageCarry_RestorePocket(struct ItemSlot *dest, const struct ItemSlot *src, u32 count)",
                              "static void CtrStorageCarry_Rollback(void)", "static void CtrStorageCarry_InitFailed(void)"):
                helpers += function(source, signature)
        (work / "storage_defs.inc").write_text(definitions)
        signatures = (
            "static void EnterPokeStorage(u8 boxOption)", "static void CB2_ReturnToPokeStorage(void)",
            "static void SetPokeStorageTask(TaskFunc newFunc)", "static void Task_InitPokeStorage(u8 taskId)",
            "static void Task_ChangeScreen(u8 taskId)", "static void FreePokeStorageData(void)",
            "static bool8 MultiMove_Init(void)", "static void MultiMove_Free(void)",
            "static bool8 IsMovingItem(void)", "static u16 GetMovingItemId(void)",
            "static void TilemapUtil_Init(u8 count)", "static void TilemapUtil_Free(void)",
            "static void MoveMon(void)", "static void PlaceMon(void)",
            "static void SetMovingMonData(u8 boxId, u8 position)", "static void PurgeMonOrBoxMon(u8 boxId, u8 position)",
            "static void SetPlacedMonData(u8 boxId, u8 position)", "static void SetShiftedMonData(u8 boxId, u8 position)",
            "static bool8 TryStorePartyMonInBox(u8 boxId)", "static void ReleaseMon(void)", "static void Task_ReleaseMon(u8 taskId)",
            "static void SaveMovingMon(void)", "static void LoadSavedMovingMon(void)",
            "static void SetSelectionAfterSummaryScreen(void)", "static void InitSummaryScreenData(void)",
            "static void Task_ShowMonSummary(u8 taskId)", "static void Task_NameBox(u8 taskId)",
            "static void InitCursor(void)", "static void InitCursorOnReopen(void)",
            "static void TakeItemFromMon(u8 cursorArea, u8 cursorPos)",
            "static void SwapItemsWithMon(u8 cursorArea, u8 cursorPos)", "static void GiveItemToMon(u8 cursorArea, u8 cursorPos)",
            "static void MoveItemFromCursorToBag(void)", "static void Task_CloseBoxWhileHoldingItem(u8 taskId)",
            "static u8 GetItemIconIdxByPosition(u8 cursorArea, u8 cursorPos)",
            "static void Task_PCMainMenu(u8 taskId)", "static bool8 SetMenuTexts_Mon(void)",
            "s16 CompactPartySlots(void)", "u8 CountPartyMons(void)", "s16 GetFirstFreeBoxSpot(u8 boxId)",
            "void SetBoxMonAt(u8 boxId, u8 boxPosition, struct BoxPokemon *src)",
            "void BoxMonAtToMon(u8 boxId, u8 boxPosition, struct Pokemon *dst)",
            "void ZeroBoxMonAt(u8 boxId, u8 boxPosition)", "struct BoxPokemon *GetBoxedMonPtr(u8 boxId, u8 boxPosition)",
            "u32 GetBoxMonDataAt(u8 boxId, u8 boxPosition, s32 request)",
            "void SetBoxMonDataAt(u8 boxId, u8 boxPosition, s32 request, const void *value)",
            "u32 GetCurrentBoxMonData(u8 boxPosition, s32 request)",
            "void SetCurrentBoxMonData(u8 boxPosition, s32 request, const void *value)",
        )
        (work / "storage_declarations.inc").write_text("\n".join(signature + ";" for signature in signatures))
        helpers += "\n".join(function(source, signature) for signature in signatures)
        helpers += function((TREE / "src/load_save.c").read_text(), "void SavePlayerParty(void)")
        item = (TREE / "src/item.c").read_text()
        helpers += "\n".join(function(item, signature) for signature in (
            "static u16 GetBagItemQuantity(u16 *quantity)", "static void SetBagItemQuantity(u16 *quantity, u16 newValue)",
            "bool8 AddBagItem(u16 itemId, u16 count)"))
        (work / "storage_helpers.inc").write_text(helpers)
        # Reuse the low-level drawing/window/allocator adapters from099.
        # The carry suite supplies real data restores/cursor initialization.
        platform = (ROOT / "android/native/test/test_storage_init.c").read_text()
        for signature in ("static void LoadSavedMovingMon(void)", "static void SetSelectionAfterSummaryScreen(void)",
                          "static void InitCursor(void)", "static void InitCursorOnReopen(void)"):
            platform = re.sub(re.escape(signature) + r" \{[^\n]*\}\n", "", platform)
        platform = replace_once(platform, '#include "storage_helpers.inc"', '#include "storage_carry_adapters.inc"\n#include "storage_helpers.inc"')
        platform = replace_once(platform, 'void FillWindowPixelBuffer(u8 id, u8 color) { assert(id == 4 && color == 0); }',
                                'void FillWindowPixelBuffer(u8 id, u8 color) { (void)id; (void)color; }')
        platform = replace_once(platform, 'static void CreateItemIconSprites(void) {}', 'static void CreateItemIconSprites(void);')
        platform = replace_once(platform, 'int main(int argc, char **argv)', 'int StorageInitRegressionMain(int argc, char **argv)')
        (work / "storage_platform.inc").write_text(platform)

        # Compile unchanged game Pokemon/RNG helpers for creation, encryption,
        # mail/held-item setters, compaction, and party recounting.
        flags = ["-D_GNU_SOURCE", "-DPORTABLE", "-DMODERN=1", "-DPORT_BRIDGE", "-DPLATFORM_3DS",
                 "-I" + str(ROOT / "android/native/include"), "-I" + str(ROOT / "android/host/include"),
                 "-iquote" + str(TREE / "include"), "-iquote" + str(TREE / "3ds_port/compat"), "-iquote" + str(TREE)]
        pre = subprocess.run([cc, "-E", "-D__INTELLISENSE__", *flags, "src/pokemon.c"],
                             cwd=TREE, capture_output=True, check=True).stdout
        converted = subprocess.run([str(TREE / "tools/preproc/preproc"), "-i", "src/pokemon.c", "charmap.txt"],
                                   cwd=TREE, input=pre, capture_output=True, check=True).stdout
        (work / "pokemon.c").write_bytes(converted)
        common = ["-std=gnu11", "-O1", "-g", "-ffunction-sections", "-fdata-sections",
                  "-fno-omit-frame-pointer", "-fsanitize=address,undefined"]
        compiler = subprocess.run([cc, "--version"], capture_output=True, text=True, check=True).stdout
        globals_flag = ["-mllvm", "-asan-globals=0"] if "clang" in compiler.lower() else ["--param=asan-globals=0"]
        subprocess.run([cc, *common, *globals_flag, "-fno-sanitize=shift", "-Wno-pointer-to-int-cast",
                        "-Wno-int-to-pointer-cast", "-Wno-attributes", "-Wno-attribute-alias",
                        "-c", str(work / "pokemon.c"), "-o", str(work / "pokemon.o")], check=True)
        binary = work / "storage-carry"
        subprocess.run([cc, *common, "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-DPORTABLE",
                        *(["-DSTORAGE_CARRY_PATCHED"] if not args.before else []),
                        "-I" + str(work), "-I" + str(ROOT / "android/native/test"), "-iquote" + str(TREE / "include"),
                        str(ROOT / "android/native/test/test_storage_carry.c"), str(TREE / "src/random.c"),
                        str(work / "pokemon.o"), "-Wl,--gc-sections", "-o", str(binary)], check=True)
        subprocess.run([str(binary), args.case], check=True, timeout=30,
                       env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
