"""The MSAA scope must contain terrain only and start with a clear target."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class VoxelMsaaOverlayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="emerald-msaa-scope-")
        cls.addClassCleanup(cls.temp.cleanup)
        tree = Path(cls.temp.name)
        target = tree / "3ds_port/src/voxel/ctr_voxel.c"
        target.parent.mkdir(parents=True)
        target.write_bytes((ROOT / "origin/3ds_port/src/voxel/ctr_voxel.c").read_bytes())
        result = subprocess.run(["git", "apply", "--verbose", str(ROOT / "patches/android/070-voxel-msaa.patch")],
                                cwd=tree, check=True, capture_output=True, text=True)
        if "offset" in result.stderr.lower() or "fuzz" in result.stderr.lower():
            raise AssertionError(result.stderr)
        cls.source = target.read_text()

    def test_scope_ends_before_billboards_and_contains_no_early_return(self):
        body = self.source[self.source.index("void CtrVoxel_Draw(C3D_RenderTarget *target, float eyeOffset)"):]
        begin = body.index("bool androidVoxelAa = CtrGpu_BeginVoxelAA(target);")
        end = body.index("if (androidVoxelAa) CtrGpu_EndVoxelAA();")
        self.assertLess(body.index("if (!sReady || sDrawCount == 0)"), begin)
        self.assertLess(body.index("C3D_FrameDrawOn(target);"), begin)
        self.assertIn("pass < VOXEL_ATLAS_PAGES + 2u", body[begin:end])
        self.assertNotIn("return", body[begin:end])
        for marker in ("if (sReflectionVertices != 0)", "if (sShadowVertices != 0)", "if (sSpriteVertices != 0)"):
            self.assertGreater(body.index(marker), end)

    def test_all_three_callers_clear_before_drawing(self):
        video = (ROOT / "origin/3ds_port/src/3ds_video.c").read_text()
        calls = list(re.finditer(r"CtrVoxel_Draw\(sLogical, 0\.0f\);", video))
        self.assertEqual(len(calls), 3)  # overworld, voxel battle, battle transition
        for call in calls:
            self.assertRegex(video[:call.start()].rstrip(), r"C2D_TargetClear\(sLogical, clear\);$")

    def test_non_android_build_has_no_new_api_dependency(self):
        # Evaluate only the conditional added by this overlay, preserving all
        # other upstream conditionals. This must reproduce the imported file.
        stripped = re.sub(r"#ifdef __ANDROID__\n.*?#endif\n", "", self.source, flags=re.S)
        original = (ROOT / "origin/3ds_port/src/voxel/ctr_voxel.c").read_text()
        self.assertEqual(stripped.replace("\n\n\n", "\n\n"), original.replace("\n\n\n", "\n\n"))


if __name__ == "__main__":
    unittest.main()
