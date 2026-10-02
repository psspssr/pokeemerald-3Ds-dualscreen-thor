from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_shim_coverage as coverage


class CoverageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="emerald-coverage-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def test_comments_strings_and_character_literals_are_not_sdk_uses(self):
        source = self.write("source.c", '''
            // C3D_Comment();
            const char *url = "https://example.invalid/C3D_String()";
            /* svcComment(); */
            char quote = '\\'';
            C3D_Real();
        ''')
        used, called = coverage.sdk_identifiers([source])
        self.assertEqual({"C3D_Real"}, used)
        self.assertEqual(used, called)

    def test_multiline_native_sources_and_full_make_rules(self):
        self.write("Makefile", "SOURCES := src/one.c \\\n  src/two.c # ignored.c\nSOURCES += src/three.c\n")
        self.write("full.mk", "build/voxel.o: src/voxel/ctr_voxel.c\n\t$(CC) $(CPPFLAGS) $(CFLAGS) -c $<\n")
        self.assertEqual({self.root / "src/one.c": "build/one.o", self.root / "src/two.c": "build/two.o",
                          self.root / "src/three.c": "build/three.o",
                          self.root / "src/voxel/ctr_voxel.c": "build/voxel.o"}, coverage.native_units(self.root))

    def test_unknown_make_syntax_fails_instead_of_underreporting(self):
        self.write("Makefile", "SOURCES := $(wildcard src/*.c)\n")
        self.write("full.mk", "")
        with self.assertRaisesRegex(ValueError, "cannot resolve"):
            coverage.native_units(self.root)

    def test_recursive_headers_include_indirect_sdk_uses_once(self):
        unit = self.write("src/main.c", '#include "one.h"\n')
        one = self.write("include/one.h", '#include "two.h"\n')
        two = self.write("include/two.h", '#include "one.h"\nC3D_Tex *texture;\n')
        self.assertEqual([one, two], coverage.local_headers(self.root, [unit]))
        self.assertEqual({"C3D_Tex"}, coverage.sdk_identifiers([one, two])[0])

    def test_nm_errors_are_not_successful_empty_symbol_sets(self):
        failure = subprocess.CompletedProcess([], 1, "", "file format not recognized")
        with patch.object(coverage.subprocess, "run", return_value=failure):
            with self.assertRaisesRegex(RuntimeError, "format not recognized"):
                coverage.nm(self.root / "invalid.elf")

    def test_origin_local_macros_do_not_require_sdk_headers(self):
        source = self.write("voxel.c", "#define C3DI_CONTEXT_FLAGS 0x20u\n#define C3DI_FLAG_TEX(unit) (1 << (unit))\n")
        self.assertTrue({"C3DI_CONTEXT_FLAGS", "C3DI_FLAG_TEX"}.issubset(coverage.self_declared([source])))

    def test_ndk_explicit_and_environment_overrides(self):
        with patch.dict(os.environ, {"ANDROID_NDK_HOME": "/ndk/from-env"}, clear=True):
            self.assertEqual(Path("/ndk/from-env"), coverage.find_ndk())
            self.assertEqual(Path("/ndk/explicit"), coverage.find_ndk(Path("/ndk/explicit")))


if __name__ == "__main__":
    unittest.main()
