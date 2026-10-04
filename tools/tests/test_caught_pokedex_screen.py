"""Exercise production callback routing through the caught-page task lifecycle."""
import ctypes
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    start = source.rfind("\n", 0, source.index(name + "(")) + 1
    end = source.index("\n}\n", start) + 3
    return source[start:end]


def pokedex_port_tail():
    """The tracked origin patch owns this whole tail; no game checkout needed."""
    patch = (ROOT / "origin/patches/pokeemerald/0005-port-hooks.patch").read_text()
    section = patch.split("diff --git a/src/pokedex.c b/src/pokedex.c\n", 1)[1]
    section = section.split("\ndiff --git ", 1)[0]
    # Recreate the final hunk at its actual output line. Everything the Android
    # overlay changes is in this origin-owned block, including context lines.
    hunks = list(re.finditer(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@[^\n]*\n", section, re.M))
    last = hunks[-1]
    tail = "".join(line[1:] for line in section[last.end():].splitlines(keepends=True)
                   if line[:1] in (" ", "+"))
    return "\n" * (int(last.group(1)) - 1) + tail


class CaughtPokedexScreenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="emerald-caught-page-")
        cls.addClassCleanup(cls.temp.cleanup)
        tree = Path(cls.temp.name)
        bridge_path = tree / "3ds_port/src/3ds_game_full.c"
        bridge_path.parent.mkdir(parents=True)
        bridge_path.write_bytes((ROOT / "origin/3ds_port/src/3ds_game_full.c").read_bytes())
        dex_path = tree / "src/pokedex.c"
        dex_path.parent.mkdir(parents=True)
        dex_path.write_text(pokedex_port_tail())
        subprocess.run(["git", "apply", str(ROOT / "patches/android/026-caught-pokedex-screen.patch")],
                       cwd=tree, check=True)
        bridge = bridge_path.read_text()
        dex = dex_path.read_text()
        # Compile the real callback registry and updater, not a duplicate policy.
        start = bridge.index("#define CTR_STAGE_CALLBACKS")
        end = bridge.index("\n}\n", bridge.index("static void UpdateStage(void)")) + 3
        registry = bridge[start:end]
        video = (ROOT / "origin/3ds_port/src/3ds_video.c").read_text()
        start = video.index("    sStage = sStageRequested;")
        geometry = video[start:video.index("    ClipToView();", start)]
        bottom_screen = function(video, "BottomWhole") + function(video, "BottomScreen")
        probe = tree / "caught_page.c"
        probe.write_text(r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "3ds_video.h"
typedef uint8_t bool8;
typedef void (*IntrCallback)(void);
typedef void (*TaskFunc)(unsigned);
static struct { IntrCallback vblankCallback; bool inBattle; } gMain;
static struct { unsigned location; } gBagPosition;
static struct { unsigned menuType; } gPartyMenu;
#define ITEMMENULOCATION_FIELD 0
#define PARTY_MENU_TYPE_FIELD 0
#define CTR_LOG_VIDEO 0
#define CtrLog_Write(...) ((void)0)
static void SetVBlankCallback(IntrCallback cb) { gMain.vblankCallback=cb; }
static unsigned requestedStage, requestedCentred, requestedBattle, requestedTransition;
void CtrVideo_SetStage(bool b) { requestedStage=b; }
void CtrVideo_SetCentred(unsigned b) { requestedCentred=b; }
void CtrVideo_SetBattle(bool b) { requestedBattle=b; }
void CtrVideo_SetTransition(bool b) { requestedTransition=b; }
static void Battle(void) {}
static void Naming(void) {}
static void Summary(void) {}
static void VBlankCB_Pokedex(void) {}
static void Unrelated(void) {}
static void Task_DisplayCaughtMonDexPage(unsigned id) { (void)id; }
static void Task_HandleCaughtMonPageInput(unsigned id) { (void)id; }
static void Task_ExitCaughtMonPage(unsigned id) { (void)id; }
static void TaskOther(unsigned id) { (void)id; }
static struct { TaskFunc func; bool active; } tasks[16];
static bool8 FuncIsActiveTask(TaskFunc func)
{
    for (unsigned i=0; i<16; ++i)
        if (tasks[i].active && tasks[i].func==func) return true;
    return false;
}
''' + function(dex, "CtrPokedex_VBlankCallback")
                  + function(dex, "CtrPokedex_CaughtPageIsOpen") + registry + bottom_screen + r'''
void reset(void)
{
    sStage.count=sCentred.count=sBattle.count=sTransition.count=0;
    sStage.on=sCentred.on=sBattle.on=sTransition.on=false;
    gMain.vblankCallback=NULL;
    memset(tasks,0,sizeof(tasks));
    requestedStage=requestedCentred=requestedBattle=requestedTransition=0;
}
void task(unsigned slot, unsigned kind, unsigned active)
{
    const TaskFunc kinds[]={NULL,Task_DisplayCaughtMonDexPage,Task_HandleCaughtMonPageInput,
                           Task_ExitCaughtMonPage,TaskOther};
    tasks[slot].func=kinds[kind]; tasks[slot].active=active!=0;
}
void callback(unsigned kind)
{
    switch(kind)
    {
    case 0: SetVBlankCallback(NULL); break;
    case 1: CtrBattle_SetVBlankCallback(Battle); break;
    case 2: CtrCentredNaming_SetVBlankCallback(Naming); break;
    case 3: CtrCentredSummary_SetVBlankCallback(Summary); break;
    case 4: CtrCentredPokedex_SetVBlankCallback(VBlankCB_Pokedex); break;
    case 5: SetVBlankCallback(Unrelated); break;
    // The caught page restores the shared battle callback through this setter.
    case 6: CtrCentredPokedex_SetVBlankCallback(Battle); break;
    }
}
void frame(float *out)
{
    UpdateStage();
    bool sStageRequested=requestedStage, sBattleRequested=requestedBattle;
    unsigned sCentredRequested=requestedCentred;
    bool sStage, sCentred, sBattle, sTransition;
    bool sTransitionRequested=requestedTransition, sLineRegs=false;
    unsigned sCentredScreen;
    float sZoom, sOffX, sOffY, sShiftZoom;
    int sViewX, sViewY;
''' + geometry + r'''
    (void)sTransition; (void)sShiftZoom;
    out[0]=sBattle; out[1]=sCentred; out[2]=sZoom;
    out[3]=sViewX*sZoom+sOffX; out[4]=sViewY*sZoom+sOffY;
    out[5]=(240+sViewX)*sZoom+sOffX; out[6]=(160+sViewY)*sZoom+sOffY;
    out[7]=BottomScreen(sCentredScreen); out[8]=sCentredScreen;
    out[9]=CtrPokedex_CaughtPageIsOpen();
}
''')
        library = tree / "caught_page.so"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                        "-I" + str(ROOT / "origin/3ds_port/include"), str(probe), "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.reset.argtypes = []
        cls.lib.task.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
        cls.lib.callback.argtypes = [ctypes.c_uint]
        cls.lib.frame.argtypes = [ctypes.POINTER(ctypes.c_float)]
        for name in ("reset", "task", "callback", "frame"):
            getattr(cls.lib, name).restype = None

    def setUp(self):
        self.lib.reset()
        self.lib.callback(1)

    def frame(self):
        result = (ctypes.c_float * 10)()
        self.lib.frame(result)
        return list(result)

    def assert_caught_page(self, frame):
        self.assertEqual(frame[:3], [0, 1, 1])  # no battle crop, centred, 1:1
        self.assertEqual(frame[3:7], [80, 40, 320, 200])  # full 240x160
        self.assertEqual(frame[7:], [0, 1, 1])  # top screen, plain, active

    def test_registration_input_and_exit_keep_every_description_row(self):
        self.assertEqual(self.frame()[0], 1)
        for phase in (1, 2, 3):
            self.lib.task(7, phase, 1)
            self.lib.callback(6)
            frame = self.frame()
            self.assert_caught_page(frame)
            # Every original row remains visible, including the description's
            # lower lines beyond the battle scene's row112 crop.
            for y in range(160):
                self.assertGreaterEqual(y + frame[4], frame[4])
                self.assertLessEqual(y + 1 + frame[4], frame[6])

    def test_destroyed_or_reused_task_restores_normal_battle_immediately(self):
        self.lib.task(15, 3, 1)
        self.assert_caught_page(self.frame())
        self.lib.task(15, 3, 0)  # inactive stale callback must not keep the mode
        self.assertEqual(self.frame()[:2], [1, 0])
        self.lib.task(15, 4, 1)  # slot reuse for an unrelated live task
        self.assertEqual(self.frame()[:2], [1, 0])

    def test_shared_battle_callback_is_never_registered_as_a_menu(self):
        self.lib.task(0, 1, 1)
        self.lib.callback(6)
        self.assert_caught_page(self.frame())
        self.lib.task(0, 0, 0)
        self.lib.callback(1)
        restored = self.frame()
        self.assertEqual(restored[:2], [1, 0])
        self.assertAlmostEqual(restored[2], 1.4, places=5)
        self.lib.callback(6)
        self.assertEqual(self.frame()[:2], [1, 0])

    def test_null_callback_during_page_fade_keeps_complete_page(self):
        self.frame()
        self.lib.task(3, 1, 1)
        self.lib.callback(0)
        self.assert_caught_page(self.frame())
        self.lib.task(3, 3, 1)
        self.assert_caught_page(self.frame())
        self.lib.task(3, 3, 0)
        self.lib.callback(1)
        self.assertEqual(self.frame()[:2], [1, 0])

    def test_naming_summary_and_normal_pokedex_keep_existing_routing(self):
        for callback, bottom in ((2, 0), (3, 1), (4, 1)):
            self.lib.callback(callback)
            frame = self.frame()
            self.assertEqual(frame[:3], [0, 1, 1])
            self.assertEqual(frame[7], bottom)
            self.lib.callback(0)  # the existing transition-gap policy survives
            self.assertEqual(self.frame()[:9], frame[:9])
            self.lib.callback(1)
            self.assertEqual(self.frame()[:2], [1, 0])
        self.lib.callback(5)
        self.assertEqual(self.frame()[:3], [0, 0, 1])


if __name__ == "__main__":
    unittest.main()
