"""Exercise the patched battle UI transform and its actual composition masks."""
import ctypes
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    start = source.index("static ", source.index(name) - 16)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


class BattleStatPanelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="emerald-battle-ui-")
        tree = Path(cls.temp.name)
        source = tree / "3ds_port/src/3ds_video.c"
        source.parent.mkdir(parents=True)
        source.write_bytes((ROOT / "origin/3ds_port/src/3ds_video.c").read_bytes())
        for patch in ("010-field-ui-wrap.patch", "025-battle-stat-panel.patch"):
            subprocess.run(["git", "apply", str(ROOT / "patches/android" / patch)], cwd=tree, check=True)
        cls.patched = source.read_text()
        classifier = function(cls.patched, "BattleStatLayer(void)")
        transform = function(cls.patched, "DrawBattleUiLayer(unsigned bg)")
        scene = function(cls.patched, "RenderBattleScene(uint32_t clear)")
        ui = function(cls.patched, "RenderEye(C3D_RenderTarget *target")
        world = function(cls.patched, "RenderBattleWorld(uint32_t clear)")
        scene_mask = re.search(r"sLayerExclude = 1u[^;]+;", scene)[0]
        ui_mask = re.search(r"sLayerExclude = 63[^;]+;", ui)[0]
        world_mask = re.search(r"sLayerExclude = sScene[^;]+;", world)[0]
        probe = tree / "battle_ui.c"
        probe.write_text(r'''
#include <assert.h>
#include <math.h>
#include "3ds_video.h"
static bool sBattle;
static unsigned display, priority0, priority1;
static unsigned sWorldLayers;
static int sViewX, sViewY, sClipX0, sClipY0, sClipX1, sClipY1;
static float sZoom, sOffX, sOffY, sLayerShift, sShiftZoom;
static float *result;
static unsigned Reg(unsigned r) { return r == 0 ? display : r == 8 ? priority0 : priority1; }
static void C2D_Flush(void) {}
static void RestoreScissor(void) {}
static void ViewBase(void) {}
static void Capture(int x0, int x1, int y0, int y1)
{
    result[0]=(x0+sViewX+sLayerShift)*sZoom+sOffX;
    result[1]=(y0+sViewY)*sZoom+sOffY;
    result[2]=(x1+sViewX+sLayerShift)*sZoom+sOffX;
    result[3]=(y1+sViewY)*sZoom+sOffY;
    result[4]=(sClipX0+sViewX)*sZoom+sOffX;
    result[5]=(sClipY0+sViewY)*sZoom+sOffY;
    result[6]=(sClipX1+sViewX)*sZoom+sOffX;
    result[7]=(sClipY1+sViewY)*sZoom+sOffY;
    result[8]=sZoom;
    result[13]=(64+sViewY)*sZoom+sOffY;
}
static void DrawBattleCut(unsigned bg,int x0,int x1,int y0,int y1,int shift)
{ assert(bg==1 && shift==0); result[10]=1; Capture(x0,x1,y0,y1); }
static void DrawBattleText(unsigned bg)
{ assert(bg==0); result[10]=0; Capture(-CTR_BATTLE_X,CTR_GAME_WIDTH-CTR_BATTLE_X,112,160); }
''' + classifier + "\n" + transform + r'''
void probe(unsigned mode, unsigned bg, float shift, float *out)
{
    result=out;
    sWorldLayers=0;
    sBattle=(mode&1)!=0;
    display=(mode&4)?0:0x200;
    priority0=(mode&2)?1:0; priority1=(mode&2)?0:1;
    sViewX=sViewY=0;
    sZoom=sShiftZoom=CTR_BATTLE_ZOOM;
    sOffX=CTR_GAME_WIDTH/2-120*sZoom;
    sOffY=CTR_GAME_HEIGHT-48-112*sZoom;
    sLayerShift=shift;
    sClipX0=(int)floorf(-sOffX/sZoom);
    sClipY0=(int)floorf(-sOffY/sZoom);
    sClipX1=(int)ceilf((CTR_GAME_WIDTH-sOffX)/sZoom);
    sClipY1=(int)ceilf((CTR_GAME_HEIGHT-sOffY)/sZoom);
    float previous[]={sZoom,sOffX,sOffY,sLayerShift};
    int clips[]={sClipX0,sClipY0,sClipX1,sClipY1};
    result[9]=BattleStatLayer();
    unsigned sLayerExclude;
''' + scene_mask + "\nresult[11]=sLayerExclude;\n" + ui_mask + r'''
    result[12]=sLayerExclude;
    DrawBattleUiLayer(bg);
    assert(sViewX==0 && sViewY==0);
    assert(sZoom==previous[0] && sOffX==previous[1] && sOffY==previous[2] && sLayerShift==previous[3]);
    assert(sClipX0==clips[0] && sClipY0==clips[1] && sClipX1==clips[2] && sClipY1==clips[3]);
}
''' + r'''
unsigned world_mask(unsigned layers, unsigned stats, unsigned cached)
{
    sBattle=true; display=0x200;
    priority0=stats?1:0; priority1=stats?0:1;
    sWorldLayers=layers;
    bool sScene=cached;
    unsigned sLayerExclude;
''' + world_mask + r'''
    return sLayerExclude;
}
unsigned scene_mask(unsigned layers, unsigned stats)
{
    sBattle=true; display=0x200;
    priority0=stats?1:0; priority1=stats?0:1;
    sWorldLayers=layers;
    unsigned sLayerExclude;
''' + scene_mask + r'''
    return sLayerExclude;
}
''')
        library = tree / "battle_ui.so"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                        "-I" + str(ROOT / "origin/3ds_port/include"), str(probe), "-lm", "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.probe.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_float, ctypes.POINTER(ctypes.c_float)]
        cls.lib.probe.restype = None
        cls.lib.world_mask.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
        cls.lib.world_mask.restype = ctypes.c_uint
        cls.lib.scene_mask.argtypes = [ctypes.c_uint, ctypes.c_uint]
        cls.lib.scene_mask.restype = ctypes.c_uint

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def geometry(self, mode, bg, shift=0):
        result = (ctypes.c_float * 14)()
        self.lib.probe(mode, bg, shift, result)
        return list(result)

    def test_all_six_stat_rows_and_outer_frame_fit_above_message(self):
        panel = self.geometry(3, 1)
        self.assertEqual(panel[:4], [296, 80, 392, 184])
        self.assertEqual(panel[8:11], [1, 1, 1])
        # Both level-up pages share B_WIN_LEVEL_UP_BOX: tile row8, then
        # six 13px glyph rows at15px intervals (menu_specialized.c).
        for stat in range(6):
            y = panel[13] + 15 * stat
            self.assertGreaterEqual(y, panel[1])
            self.assertLessEqual(y + 13, panel[3])
            self.assertGreaterEqual(y, panel[5])
            self.assertLessEqual(y + 13, panel[7])
        message = self.geometry(3, 0)
        self.assertEqual(message[:4], [0, 192, 400, 240])
        self.assertEqual(message[1] - panel[3], 8)

    def test_stat_layer_is_removed_from_scene_and_included_in_final_ui(self):
        panel = self.geometry(3, 1)
        self.assertEqual(int(panel[11]), 3)  # scene excludes BG0 and BG1
        self.assertEqual(int(panel[12]), 28)  # final UI includes both + blending
        self.assertIn("(bg == 1 && BattleStatLayer())", function(self.patched, "Layers(unsigned mask)"))

    def test_normal_battle_layout_and_masks_are_unchanged(self):
        normal = self.geometry(1, 0)
        self.assertEqual(normal[:4], [0, 192, 400, 240])
        self.assertEqual(normal[8:13], [1, 0, 0, 1, 30])

    def test_voxel_battle_keeps_world_exclusions_and_only_adds_stat_ui(self):
        # BG3 is scenery; during entry BG1/BG2 may also be replaced by the world.
        for layers in (0, 8, 14):
            for stats in (0, 1):
                self.assertEqual(self.lib.scene_mask(layers, stats), 1 | layers | (2 if stats else 0))
                self.assertEqual(self.lib.world_mask(layers, stats, 1), 28 if stats else 30)
                self.assertEqual(self.lib.world_mask(layers, stats, 0), layers & ~(2 if stats else 0))

    def test_detector_rejects_nonbattle_and_disabled_bg1(self):
        self.assertEqual(self.geometry(2, 0)[9], 0)
        self.assertEqual(self.geometry(7, 0)[9], 0)

    def test_transform_restores_clip_zoom_and_stereo_shift(self):
        panel = self.geometry(3, 1, 1)
        self.assertAlmostEqual(panel[0], 297.4, places=4)
        # Every probe also asserts all production transform/clip globals are
        # restored after the UI pass, including the normal message path.
        self.assertEqual(self.geometry(1, 0)[:4], [0, 192, 400, 240])


if __name__ == "__main__":
    unittest.main()
