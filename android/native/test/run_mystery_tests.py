#!/usr/bin/env python3
"""Exercise the event adapter with actual game inventory/Pokemon/Dex code.

Only localized item/species/decor metadata and unrelated platform callbacks
are fixtures. Pokemon generation, encrypted bag quantities, gift delivery,
Dex writes, flags and decoration insertion execute the pinned game code.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()
CC = os.environ.get("CC", "cc")
flags = ["-D_GNU_SOURCE", "-DPORTABLE", "-DMODERN=1", "-DPORT_BRIDGE", "-DPLATFORM_3DS",
         "-I" + str(ROOT / "android/native/include"), "-I" + str(ROOT / "android/host/include"),
         "-iquote" + str(TREE / "include"), "-iquote" + str(TREE / "3ds_port/compat"),
         "-iquote" + str(TREE)]


def function(path, signature):
    source = (TREE / path).read_text()
    start = source.index(signature + "\n{")
    begin = source.index("{", start)
    depth = 0
    for i in range(begin, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1] + "\n"
    raise RuntimeError("unterminated engine function: " + signature)


with tempfile.TemporaryDirectory(prefix="emerald-mystery-test-") as directory:
    work = Path(directory)
    compiler = subprocess.run([CC, "--version"], check=True, capture_output=True, text=True).stdout
    globals_flag = ["-mllvm", "-asan-globals=0"] if "clang" in compiler.lower() else ["--param=asan-globals=0"]
    common = ["-std=gnu11", "-O1", "-g", "-ffunction-sections", "-fdata-sections",
              "-fno-omit-frame-pointer", "-fsanitize=address,undefined"]
    objects = []
    # Extract independent, unchanged engine helpers to avoid retaining the
    # entire Pokedex/menu asset graph in a host unit test.
    extracted = ('#include "global.h"\n#include "pokemon.h"\n#include "pokedex.h"\n'
                 '#include "overworld.h"\n#include "event_data.h"\n'
                 '#include "constants/layouts.h"\n#include "constants/maps.h"\n'
                 '#include "constants/battle_frontier.h"\n')
    extracted += function("src/pokedex.c", "s8 GetSetPokedexFlag(u16 nationalDexNo, u8 caseID)")
    extracted += function("src/script_pokemon_util.c", "u8 ScriptGiveMon(u16 species, u8 level, u16 item, u32 unused1, u32 unused2, u8 unused3)")
    extracted += function("src/load_save.c", "void SavePlayerParty(void)")
    extracted += function("src/load_save.c", "void LoadPlayerParty(void)")
    extracted += function("src/battle_pike.c", "bool8 InBattlePike(void)")
    extracted += function("src/field_specials.c", "bool8 InMultiPartnerRoom(void)")
    (work / "gift_dex.c").write_text(extracted)
    item = (TREE / "src/item.c").read_text()
    for line in ['#include "data/text/item_descriptions.h"', '#include "data/items.h"']:
        assert item.count(line) == 1
        item = item.replace(line, "/* item metadata supplied by the host fixture */")
    (work / "item.c").write_text(item)
    sources = [TREE / "src/pokemon.c", work / "item.c", TREE / "src/event_data.c",
               TREE / "src/decoration_inventory.c", work / "gift_dex.c"]
    for index, source in enumerate(sources):
        pre = subprocess.run([CC, "-E", "-D__INTELLISENSE__", *flags, str(source)],
                             cwd=TREE, check=True, capture_output=True).stdout
        converted = subprocess.run([str(TREE / "tools/preproc/preproc"), "-i", str(source), "charmap.txt"],
                                   cwd=TREE, input=pre, check=True, capture_output=True).stdout
        final = work / ("engine_" + str(index) + ".c")
        final.write_bytes(converted)
        obj = work / ("engine_" + str(index) + ".o")
        # Existing game setters assemble u32 with signed promoted bytes.
        # New Android code/tests below retain all UBSan checks.
        subprocess.run([CC, *common, *globals_flag, "-fno-sanitize=shift", "-Wno-pointer-to-int-cast",
                        "-Wno-int-to-pointer-cast", "-Wno-attributes", "-Wno-attribute-alias",
                        "-c", str(final), "-o", str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run([CC, *common, "-Wall", "-Wextra", "-Werror", "-Wno-ignored-qualifiers", *flags,
                    str(ROOT / "android/native/test/test_mystery.c"),
                    str(ROOT / "android/native/src/mystery_events.c"),
                    str(ROOT / "android/native/src/qol_game.c"),
                    str(ROOT / "android/native/src/qol_rules.c"),
                    str(TREE / "src/random.c"), *objects,
                    "-Wl,--gc-sections", "-Wl,--wrap=AddBagItem", "-Wl,--wrap=DecorationAdd",
                    "-o", str(work / "test_mystery")], check=True)
    subprocess.run([str(work / "test_mystery")], check=True, timeout=30,
                   env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
