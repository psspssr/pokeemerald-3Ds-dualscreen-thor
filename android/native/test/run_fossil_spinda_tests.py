#!/usr/bin/env python3
"""Exercise actual tower/fossil task callbacks and Spinda asset drawing.

The packed images are synthetic; x/y metadata, task and pixel code come from
the generated game. No save, Android device or generated tree is modified.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

from run_summary_tests import function

ROOT = Path(__file__).resolve().parents[3]
TREE = Path(os.environ.get("EMERALD_TEST_TREE", ROOT / "build/upstream")).resolve()
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import PatchError, strict_apply


def declaration(source, name):
    match = re.search(r"(?:static )?(?:const )?" + re.escape(name) + r".*?\n\};", source, re.S)
    if not match:
        raise ValueError("missing declaration: " + name)
    return match[0] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true")
    parser.add_argument("--case", choices=("all", "tower", "fossil", "spinda"), default="all")
    parser.add_argument("--variant", choices=("all", "external", "embedded", "gba"), default="all")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="emerald-fossil-spinda-") as folder:
        work = Path(folder)
        for rel in ("src/mirage_tower.c", "src/pokemon.c"):
            target = work / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((TREE / rel).read_bytes())
        patch = ROOT / "patches/android/091-fossil-spinda-assets.patch"
        try:
            strict_apply(work, patch, ["--reverse"])
        except PatchError:
            strict_apply(work, patch)
            strict_apply(work, patch, ["--reverse"])
        if not args.before:
            strict_apply(work, patch)
        fossil = (work / "src/mirage_tower.c").read_text()
        pokemon = (work / "src/pokemon.c").read_text()
        header = (TREE / "include/pokemon.h").read_text()
        data = declaration(header, "struct SpindaSpot\n")
        data += declaration(fossil, "struct FallAnim_Fossil\n")
        data += declaration(fossil, "struct FallAnim_Tower\n")
        data += declaration(fossil, "struct BgRegOffsets\n")
        data += "static struct FallAnim_Fossil *sFallingFossil;\n"
        data += "static struct FallAnim_Tower *sFallingTower;\n"
        data += "static struct BgRegOffsets *sBgShakeOffsets;\n"
        data += "static u8 *sMirageTowerGfxBuffer, *sMirageTowerTilemapBuffer;\n"
        data += re.search(r"^#define FOSSIL_DISINTEGRATE_LENGTH .*$", fossil, re.M)[0] + "\n"
        size_macro = re.search(r"#ifdef PORT_BRIDGE\n#define FOSSIL_GFX_LENGTH .*?\n#endif", fossil, re.S)
        if size_macro:
            data += size_macro[0] + "\n"
        tower_macro = re.search(r"#ifdef PORT_BRIDGE\n#define MIRAGE_TOWER_GFX_LENGTH .*?\n#endif", fossil, re.S)
        data += (tower_macro[0] if tower_macro else re.search(r"^#define MIRAGE_TOWER_GFX_LENGTH .*$", fossil, re.M)[0]) + "\n"
        data += "\n".join(re.findall(r"^#define (?:OUTER|INNER)_BUFFER_LENGTH .*$", fossil, re.M)) + "\n"
        images = [[sum(1 << col for col in range(2, 14) if (row * 3 + col + spot) % 5 == 0)
                   if 2 <= row < 14 else 0 for row in range(16)] for spot in range(4)]
        data += "static const u16 spotImages[4][16] = {" + ",".join(
            "{" + ",".join(map(str, image)) + "}" for image in images) + "};\n"
        spots = declaration(pokemon, "struct SpindaSpot gSpindaSpotGraphics[]")
        paths = re.findall(r'INCBIN_U16\("([^"]+)"\)', spots)
        assert paths == [f"graphics/pokemon/spinda/spots/spot_{i}.1bpp" for i in range(4)]
        # Exercise the real packer too: the grouped payload is four raw
        # images, not four serialized structs containing x/y coordinates.
        spec = importlib.util.spec_from_file_location("asset_pair_packer", ROOT / "origin/tools/port_common/gen_asset_table.py")
        packer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(packer)
        packer.ROOT, packer.FS_DIR = work / "inputs", work / "packed"
        packed_images = [struct.pack("<16H", *image) for image in images]
        for path, image in zip(paths, packed_images):
            target = packer.ROOT / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(image)
        grouped = packer.copy_or_concat_asset("build/root/src/pokemon.o", "gSpindaSpotGraphics", paths)
        assert grouped[1:] == (128, [0, 32, 64, 96])
        assert (packer.FS_DIR / grouped[0]).read_bytes() == b"".join(packed_images)
        tower_rule = (TREE / "graphics_file_rules.mk").read_text().split("$(MISCGFXDIR)/mirage_tower.4bpp:", 1)[1].split("\n\n", 1)[0]
        assert "-num_tiles 73 " in tower_rule
        assert struct.unpack(">II", (TREE / "graphics/object_events/pics/misc/fossil.png").read_bytes()[16:24]) == (16, 16)
        embedded_spots = spots
        for path, image in zip(paths, images):
            embedded_spots = embedded_spots.replace('INCBIN_U16("' + path + '")',
                "{" + ",".join(map(str, image)) + "}")
        stub_spots = re.sub(r'INCBIN_U16\("[^"]+"\)', "{0}", spots)
        payload = ",".join(str((i * 29 + 7) & 255) for i in range(128))
        tower_payload = ",".join(str((i * 13 + 11) & 255) for i in range(2336))
        data += "static const u8 fossilPayload[128] = {" + payload + "};\n"
        data += "static const u8 towerPayload[2336] = {" + tower_payload + "};\n"
        data += "#if TEST_EXTERNAL\n" + stub_spots + "static const u8 sFossil_Gfx[] = {0};\n"
        data += "static const u8 sMirageTower_Gfx[] __attribute__((aligned(2))) = {0};\n"
        data += "#else\n" + embedded_spots + "static const u8 sFossil_Gfx[] = {" + payload + "};\n"
        data += "static const u8 sMirageTower_Gfx[] __attribute__((aligned(2))) = {" + tower_payload + "};\n#endif\n"
        (work / "asset_pair_data.inc").write_text(data)
        helpers = ""
        bios = (TREE / "src/platform/bios.c").read_text()
        for signature in ("static uint32_t CPUReadMemory(const void *src)",
                          "static void CPUWriteMemory(void *dest, uint32_t val)",
                          "static uint16_t CPUReadHalfWord(const void *src)",
                          "static void CPUWriteHalfWord(void *dest, uint16_t val)",
                          "void CpuSet(const void *src, void *dst, u32 cnt)"):
            helpers += function(bios, signature)
        for signature in ("static void InitMirageTowerShake(u8 taskId)",
                          "static void DoMirageTowerDisintegration(u8 taskId)",
                          "static void Task_FossilFallAndSink(u8 taskId)",
                          "static void SpriteCB_FallingFossil(struct Sprite *sprite)",
                          "static void UpdateDisintegrationEffect(u8 *tiles, u16 randId, u8 c, u8 size, u8 offset)"):
            helpers += function(fossil, signature)
        start = pokemon.index("// Spots can be drawn on Spinda's color indexes")
        end = pokemon.index("// Same as DrawSpindaSpots but attempts", start)
        helpers += pokemon[start:end]
        helpers += function(pokemon, "void DrawSpindaSpots(u16 species, u32 personality, u8 *dest, bool8 isFrontPic)")
        (work / "asset_pair_helpers.inc").write_text(helpers)
        variants = ("external", "embedded", "gba") if args.variant == "all" else (args.variant,)
        for variant in variants:
            binary = work / variant
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-implicit-fallthrough", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            "-DTEST_EXTERNAL=" + str(int(variant == "external")),
                            *(["-DPORT_BRIDGE"] if variant != "gba" else []),
                            "-I" + str(work), "-iquote" + str(TREE / "include"),
                            str(ROOT / "android/native/test/test_fossil_spinda.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary), args.case], check=True, timeout=10,
                           env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
