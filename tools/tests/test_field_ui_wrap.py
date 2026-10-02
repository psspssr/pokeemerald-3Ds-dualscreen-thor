"""Apply the tracked overlay and exercise its actual field-window geometry."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FieldUiWrapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="emerald-field-ui-")
        tree = Path(cls.temp.name)
        source = tree / "3ds_port/src/3ds_video.c"
        source.parent.mkdir(parents=True)
        source.write_bytes((ROOT / "origin/3ds_port/src/3ds_video.c").read_bytes())
        subprocess.run(["git", "apply", str(ROOT / "patches/android/010-field-ui-wrap.patch")],
                       cwd=tree, check=True)
        patched = source.read_text()
        start = patched.index("static int FieldUiBottom(")
        end = patched.index("\n}\n", start) + 3
        helper = patched[start:end]
        probe = tree / "field_ui.c"
        probe.write_text("""
#include <stdbool.h>
static bool sFieldUi, sFieldBanner;
enum { VOXEL_OBJ_NONE, VOXEL_OBJ_WEATHER };
static unsigned sVoxelObjPass, control, scroll;
static unsigned Reg(unsigned reg) { return reg == 8 ? control : scroll; }
""" + helper + """
int extent(unsigned bg, int bottom, unsigned mode, unsigned size, unsigned offset)
{
    sFieldUi = (mode & 1) != 0;
    sVoxelObjPass = (mode & 2) ? VOXEL_OBJ_WEATHER : VOXEL_OBJ_NONE;
    sFieldBanner = (mode & 4) != 0;
    control = size;
    scroll = offset;
    return FieldUiBottom(bg, bottom);
}
""")
        library = tree / "field_ui.so"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                        str(probe), "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.extent.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
        cls.lib.extent.restype = ctypes.c_int
        cls.patched = patched

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_popup_slide_has_no_second_copy_on_either_field_path(self):
        for mode in (1, 2):  # cached 2D field / voxel overlay tile walk
            for scroll in range(0, 41, 2):
                with self.subTest(mode=mode, scroll=scroll):
                    bottom = self.lib.extent(0, 240, mode, 0, scroll)
                    self.assertEqual(bottom, min(240, 256 - scroll))
                    # Every shown row addresses the original window map, never
                    # a wrapped copy whose source row starts again at zero.
                    self.assertLessEqual(bottom + scroll, 256)

    def test_normal_dialogue_keeps_all_240_rows(self):
        self.assertEqual(self.lib.extent(0, 240, 1, 0, 0), 240)
        self.assertEqual(self.lib.extent(0, 240, 2, 0, 0), 240)

    def test_terrain_field_move_banner_and_other_screens_keep_wrap(self):
        for mode in (1, 2):
            for bg in (1, 2, 3):
                self.assertEqual(self.lib.extent(bg, 240, mode, 0, 40), 240)
            self.assertEqual(self.lib.extent(0, 240, mode | 4, 0, 40), 240)
        self.assertEqual(self.lib.extent(0, 240, 0, 0, 40), 240)

    def test_existing_clips_and_larger_maps_are_preserved(self):
        self.assertEqual(self.lib.extent(0, 160, 1, 0, 40), 160)
        self.assertEqual(self.lib.extent(0, 240, 1, 0x8000, 40), 240)
        # BG0's register spans512 even when its actual map is256 rows high.
        self.assertEqual(self.lib.extent(0, 240, 1, 0, 256 + 40), 216)

    def test_patch_reaches_cached_and_tile_fallback_draws(self):
        for name, invocation in (("DrawTextBg", "bottom = FieldUiBottom(bg, bottom);"),
                                 ("DrawFieldBgTex", "y1 = FieldUiBottom(bg, y1);")):
            start = self.patched.index(f"{name}(unsigned bg)\n")
            end = self.patched.index("\n}\n", start)
            self.assertIn(invocation, self.patched[start:end])


if __name__ == "__main__":
    unittest.main()
