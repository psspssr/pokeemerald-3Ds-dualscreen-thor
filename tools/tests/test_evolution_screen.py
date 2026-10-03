"""Evolution's actual build flags and the production centred viewport clip."""
import ctypes
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class EvolutionScreenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="emerald-evolution-view-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.tree = tree = Path(cls.temp.name)
        makefile = tree / "3ds_port/full.mk"
        makefile.parent.mkdir(parents=True)
        makefile.write_bytes((ROOT / "origin/3ds_port/full.mk").read_bytes())
        applied = subprocess.run(["git", "apply", "--verbose",
                                  str(ROOT / "patches/android/051-evolution-screen.patch")],
                                 cwd=tree, check=True, capture_output=True, text=True)
        assert "offset" not in applied.stderr
        source = makefile.read_text()
        start = source.index("CTR_GBA_CENTRED_SRCS :=")
        end = source.index("# Which centred screen", start)
        # Evaluate the real target-specific make assignments. This catches a
        # source-list change that fails to reach this translation unit's flags.
        probe = tree / "probe.mk"
        probe.write_text(source[start:end] + "\nbuild/root/src/evolution_scene.o:\n"
                         "\t@printf '%s\\n' '$(FULLCFLAGS)'\n")
        (tree / "compat").mkdir()
        (tree / "compat/ctr_gba_centred.h").write_bytes(
            (ROOT / "origin/3ds_port/compat/ctr_gba_centred.h").read_bytes())
        (tree / "include/gba").mkdir(parents=True)
        (tree / "include/gba/defines.h").touch()  # make prerequisite only
        made = subprocess.run(["make", "--no-print-directory", "-s", "-f", str(probe),
                               "ROOT=" + str(tree), "build/root/src/evolution_scene.o"],
                              cwd=tree, check=True, capture_output=True, text=True)
        cls.flags = shlex.split(made.stdout.strip())

        video = (ROOT / "origin/3ds_port/src/3ds_video.c").read_text()
        start = video.index("static void DrawTextBg(unsigned bg)\n")
        draw = video[start:video.index("\n}\n", start) + 3]
        start = video.index("    sStage = sStageRequested;")
        view = video[start:video.index("    ClipToView();", start)]
        c = tree / "evolution_view.c"
        c.write_text(r'''
#include <stdbool.h>
#include "3ds_video.h"
static bool sStage,sCentred,sBattle,sFieldBanner;
static unsigned sCentredScreen;
static int sClipX0,sClipX1=400,sClipY0,sClipY1=240,sViewX,sViewY;
static float sZoom,sOffX,sOffY,sShiftZoom;
static float *result;
static unsigned Min(unsigned a,unsigned b) {return a<b?a:b;}
static unsigned Reg(unsigned r) {(void)r;return 0;}
static void DrawStageBg(unsigned bg) {(void)bg;}
static void DrawBattleText(unsigned bg) {(void)bg;}
static void DrawBattleBg(unsigned bg) {(void)bg;}
static bool DrawCentredBg(unsigned bg) {(void)bg;return false;}
static bool CentredTexture(unsigned bg) {(void)bg;return false;}
static void DrawCentredPicture(unsigned bg,int top,int bottom) {(void)bg;(void)top;(void)bottom;}
static void DrawTextSpan(unsigned bg,int left,int right,int top,int bottom)
{
    (void)bg;
    result[0]=left;result[1]=right;result[2]=top;result[3]=bottom;
    result[4]=left+sViewX;result[5]=right+sViewX;
    result[6]=top+sViewY;result[7]=bottom+sViewY;
    result[8]=120+sViewX;result[9]=64+sViewY;
}
''' + draw + r'''
void probe(unsigned centred,float *out)
{
    result=out;
    bool sStageRequested=false,sBattleRequested=false,sTransitionRequested=false;
    bool sLineRegs=false,sTransition;
    unsigned sCentredRequested=centred?CTR_CENTRED_PLAIN:CTR_CENTRED_NONE;
''' + view + r'''
    (void)sTransition;(void)sShiftZoom;
    sClipX0=-sViewX;sClipX1=400-sViewX;
    sClipY0=-sViewY;sClipY1=240-sViewY;
    DrawTextBg(0);
}
''')
        library = tree / "evolution_view.so"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                        "-I" + str(ROOT / "origin/3ds_port/include"), str(c), "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.probe.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_float)]
        cls.lib.probe.restype = None

    def test_evolution_build_flags_redirect_both_callbacks_to_centred_top(self):
        self.assertIn("-DCTR_GBA_STAGE", self.flags)
        c = self.tree / "callback.c"
        c.write_text("typedef void (*Callback)(void);\n"
                     "void SetVBlankCallback(Callback);\n"
                     "void normal(void); void trade(void);\n"
                     "void scene(void) { SetVBlankCallback(0); SetVBlankCallback(normal); "
                     "SetVBlankCallback(0); SetVBlankCallback(trade); }\n")
        preprocessed = subprocess.run(["cc", "-E", "-P", *self.flags, str(c)],
                                      check=True, capture_output=True, text=True).stdout
        self.assertEqual(preprocessed.count("CtrCentred_SetVBlankCallback("), 5)
        self.assertNotIn("CtrCentredSummary_SetVBlankCallback", preprocessed)

    def test_dialogue_padding_is_clipped_and_sprite_is_centred(self):
        def geometry(mode):
            out = (ctypes.c_float * 10)()
            self.lib.probe(mode, out)
            return list(out)
        before = geometry(0)
        self.assertEqual(before[:4], [0, 256, 0, 240])  # exposed unused columns240..255
        after = geometry(1)
        self.assertEqual(after[:4], [0, 240, 0, 160])
        self.assertEqual(after[4:8], [80, 320, 40, 200])  # whole picture, never cropped
        self.assertEqual(after[8:], [200, 104])  # fixed game sprite (120,64), centred


if __name__ == "__main__":
    unittest.main()
