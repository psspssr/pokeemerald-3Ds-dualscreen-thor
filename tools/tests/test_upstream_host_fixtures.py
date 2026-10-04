"""Keep upstream's host probes usable without modifying the imported snapshot."""
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from bootstrap import strict_apply


class UpstreamHostFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="upstream-host-fixture-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.tree = Path(cls.temporary.name)
        cls.port = cls.tree / "3ds_port"
        for rel in ("src/3ds_bottom_ui.c", "tests/bottom_map_test.py", "tests/voxel_runtime_test.py"):
            target = cls.port / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / "origin/3ds_port" / rel, target)
        (cls.port / "build").mkdir()
        subprocess.run(["git", "init", "-q", str(cls.tree)], check=True)
        strict_apply(cls.tree, ROOT / "patches/android/060-upstream-host-test-extraction.patch")
        cls.extract = staticmethod(runpy.run_path(str(cls.port / "tests/voxel_runtime_test.py"))["function"])

    def test_forward_declaration_does_not_capture_an_unrelated_body(self):
        body = "static int chosen(int x) { return x + 1; }\n"
        source = "static int chosen(int x);\nstatic void unrelated(void) {}\n" + body
        self.assertEqual(body, self.extract(source, "static int chosen("))
        self.assertEqual(body, self.extract(source, "static int chosen(int x)\n{"))
        with self.assertRaisesRegex(ValueError, "missing function definition"):
            self.extract("static int missing(int x);\n", "static int missing(")

    def test_actual_bottom_map_dirty_rect_probe_passes(self):
        result = subprocess.run([sys.executable, str(self.port / "tests/bottom_map_test.py"), "--cc", "cc"],
                                check=True, capture_output=True, text=True)
        self.assertIn("partial/full canvas and framebuffer equivalence", result.stdout)


if __name__ == "__main__":
    unittest.main()
