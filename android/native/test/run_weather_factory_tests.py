#!/usr/bin/env python3
"""Sanitize actual weather/Factory consumers with real external and embedded assets."""
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
from bootstrap import PatchError, patch_files, strict_apply


def array(name, data):
    values = struct.unpack("<%dH" % (len(data) // 2), data)
    return "static const u16 %s[] = {%s};\n" % (name, ",".join(hex(v) for v in values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", action="store_true")
    parser.add_argument("--case", choices=("all", "weather", "weather-blend", "factory-fade", "factory-select", "factory-swap"), default="all")
    parser.add_argument("--mode", choices=("all", "external", "embedded"), default="all")
    args = parser.parse_args()
    patch = ROOT / "patches/android/089-weather-factory-assets.patch"
    with tempfile.TemporaryDirectory(prefix="emerald-weather-factory-") as directory:
        work = Path(directory)
        for rel in patch_files(patch):
            p = work / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes((TREE / rel).read_bytes())
        try:
            strict_apply(work, patch, ["--reverse"])
        except PatchError:
            strict_apply(work, patch)
            strict_apply(work, patch, ["--reverse"])
        if not args.before:
            strict_apply(work, patch)
        weather = (work / "src/field_weather.c").read_text()
        factory = (work / "src/battle_factory_screen.c").read_text()
        helpers = re.search(r"^#define DROUGHT_COLOR_INDEX.*$", weather, re.M)[0] + "\n"
        helpers += re.search(r"struct RGBColor\s*\{.*?\};", weather, re.S)[0] + "\n"
        helpers += function(weather, "static void ApplyColorMap(u8 startPalIndex, u8 numPalettes, s8 colorMapIndex)")
        helpers += function(weather, "static void ApplyDroughtColorMapWithBlend(s8 colorMapIndex, u8 blendCoeff, u16 blendColor)")
        if "static u16 FactorySwapFadeColor(void)" in factory:
            helpers += function(factory, "static u16 FactorySwapFadeColor(void)")
            helpers += function(factory, "static u32 FactoryAssetSize(const void *asset, u32 embeddedSize)")
            helpers += re.search(r"^#define FACTORY_ASSET_SIZE.*$", factory, re.M)[0] + "\n"
        fade = re.findall(r"^\s*(?:BeginNormalPaletteFade|gPlttBufferFaded)[^\n]*(?:sPokeballGray_Pal\[37\]|FactorySwapFadeColor\(\))[^\n]*;", factory, re.M)
        assert len(fade) == 3, "all three real Factory fade consumers must be tested"
        helpers += "static void FactoryFadeConsumers(void) {\n" + "\n".join(fade) + "\n}\n"
        for mode in ("Select", "Swap"):
            body = function(factory, "static void CB2_Init%sScreen(void)" % mode)
            first = body.index("        s%sMenuTilesetBuffer = Alloc(" % mode)
            end = body.index("        s%sMenuTilemapBuffer = Alloc(" % mode)
            allocation = body[first:end]
            copy = re.findall(r"^\s*(?:CpuCopy16|LoadBgTiles)\([^\n]*s%s(?:Menu|MonPicBg)TilesetBuffer[^\n]*;" % mode, body, re.M)
            assert len(copy) == 4
            helpers += "static void Factory%sBuffers(void) {\n" % mode + allocation + "\n".join(copy) + "\n}\n"
        (work / "weather_factory_helpers.inc").write_text(helpers)
        bios = (TREE / "src/platform/bios.c").read_text()
        (work / "weather_factory_bios.inc").write_text("\n".join(function(bios, sig) for sig in (
            "static uint32_t CPUReadMemory(const void *src)", "static void CPUWriteMemory(void *dest, uint32_t val)",
            "static uint16_t CPUReadHalfWord(const void *src)", "static void CPUWriteHalfWord(void *dest, uint16_t val)",
            "void CpuSet(const void *src, void *dst, u32 cnt)")))
        payload = ""
        for name, path in (("menuData", "menu.4bpp"), ("pictureData", "mon_pic_bg.4bpp"),
                           ("interfaceData", "interface.gbapal"), ("grayData", "pokeball_gray.gbapal")):
            payload += array(name, (TREE / "graphics/battle_frontier/factory_screen" / path).read_bytes())
        drought = b"".join((TREE / ("graphics/weather/drought/colors_%d.bin" % i)).read_bytes() for i in range(6))
        assert len(drought) == 6 * 4096 * 2
        payload += array("droughtData", drought)
        (work / "weather_factory_data.inc").write_text(payload)

        # Use the actual packer, not a separate fixture parser: the grouped
        # declaration must produce one correctly ordered 49152-byte asset.
        spec = importlib.util.spec_from_file_location("asset_table", ROOT / "origin/tools/port_common/gen_asset_table.py")
        packer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(packer)
        packer.ROOT = work
        packer.BUILD_EMERALD = TREE
        packer.FS_DIR = work / "romfs"
        entries = [e for e in packer.parse_incbins_by_source() if e[1] == "sDroughtWeatherColors"]
        assert len(entries) == 1 and len(entries[0][2]) == 6
        for relative in entries[0][2]:
            destination = work / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes((TREE / relative).read_bytes())
        path, size, offsets = packer.copy_or_concat_asset("build/root/src/field_weather.o", entries[0][1], entries[0][2])
        assert size == len(drought) and offsets == [i * 8192 for i in range(6)]
        assert (packer.FS_DIR / path).read_bytes() == drought
        for external in (True, False):
            label = "external" if external else "embedded"
            if args.mode not in ("all", label):
                continue
            for bugfix in (False, True):
                binary = work / (label + ("-bugfix" if bugfix else "-original-flags"))
                subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                                "-Wno-array-bounds", "-fno-strict-aliasing", "-fsanitize=address,undefined",
                                "-fno-omit-frame-pointer", *(["-DPORT_BRIDGE"] if external else []),
                                *(["-DBUGFIX"] if bugfix else []), "-I" + str(work),
                                str(ROOT / "android/native/test/test_weather_factory_assets.c"), "-o", str(binary)], check=True)
                subprocess.run([str(binary), args.case], check=True, timeout=15,
                               env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()
