"""Resolution follows the actual render dispatch without altering upstream coordinates."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import patch_files, strict_apply

class VoxelResolutionOverlayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="emerald-resolution-scope-")
        cls.addClassCleanup(cls.temp.cleanup)
        tree = Path(cls.temp.name)
        path = tree / "3ds_port/src/3ds_video.c"
        path.parent.mkdir(parents=True)
        path.write_bytes((ROOT / "origin/3ds_port/src/3ds_video.c").read_bytes())
        for patch in sorted((ROOT / "patches/android").glob("*.patch")):
            if patch.name[:3] < "073" and "3ds_port/src/3ds_video.c" in patch_files(patch):
                strict_apply(tree, patch, ("--include=3ds_port/src/3ds_video.c",))
        cls.before = path.read_text()
        strict_apply(tree, ROOT / "patches/android/073-voxel-resolution.patch")
        cls.after = path.read_text()

    def test_dispatch_is_after_mode_decision_before_any_output(self):
        call = self.after.index("CtrGpu_ConfigureVoxelTargets(sLogical, sTop,")
        self.assertEqual(self.after.count("CtrGpu_ConfigureVoxelTargets("), 1)
        self.assertGreater(call, self.after.index("sBattleWorld = battleUpdated"))
        self.assertGreater(call, self.after.index("PORT_PROF_BEGIN(draw);"))
        self.assertLess(call, self.after.index("RenderEye(sBottom, clear, 0.0f);"))
        self.assertIn("!bottom && (voxel || sBattleWorld), bottom", self.after[call:call+150])

    def test_non_android_source_and_logical_coordinates_are_unchanged(self):
        include = "#ifdef __ANDROID__\n#include <ctr_gpu_voxel.h>\n#endif\n"
        hook = re.search(r"#ifdef __ANDROID__\n    /\* Retain the held top image.*?#endif\n", self.after, re.S)
        self.assertIsNotNone(hook)
        self.assertEqual(self.after.replace(include, "").replace(hook.group(0), ""), self.before)

    def test_actual_hook_keeps_bottom_menus_and_classic_frames_native(self):
        call = re.search(r"CtrGpu_ConfigureVoxelTargets\(sLogical, sTop,.*?;", self.after).group(0)
        source = """#include <assert.h>
#include <stdbool.h>
static bool observedVoxel, observedKeep;
static void CtrGpu_ConfigureVoxelTargets(void *a, void *b, bool voxel, bool keep) {
    assert(a != b); observedVoxel=voxel; observedKeep=keep;
}
int main(void) {
    int a,b; void *sLogical=&a,*sTop=&b;
    for(int bits=0;bits<8;bits++) {
        bool bottom=(bits&1)!=0, voxel=(bits&2)!=0, sBattleWorld=(bits&4)!=0;
""" + call + """
        if(bottom) assert(!observedVoxel && observedKeep);
        else assert(observedVoxel==(voxel || sBattleWorld) && !observedKeep);
    }
}
"""
        tree = Path(self.temp.name)
        (tree / "dispatch.c").write_text(source)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(tree / "dispatch.c"), "-o", str(tree / "dispatch")], check=True)
        subprocess.run([str(tree / "dispatch")], check=True)

if __name__ == "__main__":
    unittest.main()
