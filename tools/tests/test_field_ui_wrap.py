"""Exercise upstream's actual popup tile clipping after retiring Android010."""
import ctypes
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    match = re.search(re.escape(signature) + r"\s*\{", text)
    if not match:
        raise ValueError("missing upstream definition: " + signature)
    brace = text.index("{", match.start())
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end]


class FieldUiWrapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="emerald-field-ui-")
        tree = Path(cls.temp.name)
        patch = (ROOT / "origin/patches/pokeemerald/0005-port-hooks.patch").read_text()
        # Both complete functions are additions in upstream's immutable patch.
        additions = "\n".join(line[1:] for line in patch.splitlines()
                              if line.startswith("+") and not line.startswith("+++"))
        cls.additions = additions
        actual = function(additions, "static void SavePopUpTilemap(void)") + "\n"
        actual += function(additions, "static void ClipPopUpTilemap(s16 yOffset)")
        probe = tree / "field_ui.c"
        probe.write_text("""
#include <stdint.h>
typedef uint16_t u16;
typedef int16_t s16;
typedef int32_t s32;
typedef uint32_t bool32;
#define WINDOW_NONE 255
#define POPUP_TILE_ROWS 5
#define POPUP_TILE_COLS 12
#define BG_MAP_WIDTH 32
#define TILE_HEIGHT 8
#define DISPLAY_HEIGHT 240
static u16 map[32 * 32], sPopUpTilemap[5][12];
static unsigned uploads, window;
static u16 *GetBgTilemapBuffer(unsigned bg) { (void)bg; return map; }
static unsigned GetMapNamePopUpWindowId(void) { return window; }
static void CopyBgTilemapBufferToVram(unsigned bg) { (void)bg; ++uploads; }
""" + actual + """
void reset(void) {
    for (unsigned i = 0; i < 32 * 32; ++i) map[i] = i + 1;
    uploads = 0; window = 0; SavePopUpTilemap();
}
void clip(int offset) { ClipPopUpTilemap(offset); }
void no_window(void) { window = WINDOW_NONE; }
unsigned cell(unsigned row, unsigned col) { return map[row * 32 + col]; }
unsigned copies(void) { return uploads; }
""")
        library = tree / "field_ui.so"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                        str(probe), "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.clip.argtypes = [ctypes.c_int]
        cls.lib.cell.argtypes = [ctypes.c_uint, ctypes.c_uint]
        cls.lib.cell.restype = cls.lib.copies.restype = ctypes.c_uint

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.lib.reset()

    def test_visible_wrapped_pixels_are_blank_at_every_slide_offset(self):
        for offset in range(41):
            self.lib.clip(offset)
            for y in range(240):
                source_y = (y + offset) % 256
                if source_y < 40:
                    for col in range(12):
                        cell = self.lib.cell(source_y // 8, col)
                        if source_y < offset:
                            self.assertEqual(cell, 0, (offset, y, col))
                        else:
                            self.assertEqual(cell, (source_y // 8) * 32 + col + 1)

    def test_slide_back_in_restores_the_saved_window(self):
        self.lib.clip(40)
        self.lib.clip(0)
        for row in range(5):
            for col in range(12):
                self.assertEqual(self.lib.cell(row, col), row * 32 + col + 1)

    def test_surrounding_field_tiles_are_untouched(self):
        self.lib.clip(40)
        for row in range(32):
            for col in range(32):
                if row >= 5 or col >= 12:
                    self.assertEqual(self.lib.cell(row, col), row * 32 + col + 1)

    def test_no_window_does_not_modify_or_upload_the_map(self):
        self.lib.no_window()
        self.lib.clip(40)
        self.assertEqual(self.lib.copies(), 0)
        self.assertEqual(self.lib.cell(0, 0), 1)

    def test_upstream_task_installs_and_updates_its_clipping(self):
        self.assertEqual(self.additions.count("SavePopUpTilemap();"), 1)
        self.assertEqual(self.additions.count("ClipPopUpTilemap(task->tYOffset);"), 3)
        self.assertFalse((ROOT / "patches/android/010-field-ui-wrap.patch").exists())


if __name__ == "__main__":
    unittest.main()
