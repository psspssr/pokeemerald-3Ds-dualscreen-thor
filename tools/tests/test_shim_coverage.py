from pathlib import Path
import os
import shutil
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

    def build_gc_fixture(self, missing=False):
        if not shutil.which("cc") or not shutil.which("nm"):
            self.skipTest("host C compiler and nm required for link coverage regression")
        self.write("3ds_port/native.c", """
            extern int __ctru_heap;
            extern void svcControlMemory(void);
            extern void svcMissing(void);
            void unused_3ds_startup(void) {
                __ctru_heap = 1;
                svcControlMemory();
                %s
            }
            void entry(void) {}
        """ % ("svcMissing();" if missing else ""))
        self.write("3ds_port/sdk.c", "int __ctru_heap; void svcControlMemory(void) {}\n")
        self.write("3ds_port/stale.c", "void svcMissing(void) {}\n")
        native = self.root / "3ds_port/build/native.o"
        sdk = self.root / "3ds_port/build/android/shim/src/sdk.o"
        stale = self.root / "3ds_port/build/android/shim/src/stale.o"
        sdk.parent.mkdir(parents=True)
        for source, output in (("native", native), ("sdk", sdk), ("stale", stale)):
            subprocess.run(["cc", "-ffunction-sections", "-fdata-sections", "-c",
                            str(self.root / f"3ds_port/{source}.c"), "-o", str(output)], check=True)
        library = self.root / "3ds_port/library.elf"
        subprocess.run(["cc", "-nostdlib", "-no-pie", "-Wl,--gc-sections", "-Wl,-e,entry",
                        str(native), str(sdk), "-o", str(library)], check=True)
        self.write("3ds_port/build/link.rsp", "build/native.o build/android/shim/src/sdk.o -lc\n")
        return native, library, sdk

    def test_dead_startup_dependencies_have_real_sdk_definitions_before_gc(self):
        native, library, sdk = self.build_gc_fixture()
        needed = coverage.nm(native, "-u", tool="nm")
        final = coverage.nm(library, "--defined-only", tool="nm")
        self.assertEqual({"__ctru_heap", "svcControlMemory"}, needed - final)
        objects = coverage.linked_sdk_objects(self.root)
        self.assertEqual([sdk], objects)
        provided = final.copy()
        for obj in objects:
            provided |= coverage.nm(obj, "-g", "--defined-only", tool="nm")
        self.assertEqual(set(), needed - provided)

    def test_missing_definition_is_not_hidden_by_stale_unlinked_sdk_object(self):
        native, library, _ = self.build_gc_fixture(missing=True)
        provided = coverage.nm(library, "--defined-only", tool="nm")
        for obj in coverage.linked_sdk_objects(self.root):
            provided |= coverage.nm(obj, "-g", "--defined-only", tool="nm")
        self.assertEqual({"svcMissing"}, coverage.nm(native, "-u", tool="nm") - provided)

    def test_missing_linked_sdk_object_fails_closed(self):
        self.write("3ds_port/build/link.rsp", "build/android/shim/src/absent.o\n")
        with self.assertRaisesRegex(ValueError, "linked SDK object missing"):
            coverage.linked_sdk_objects(self.root)


if __name__ == "__main__":
    unittest.main()
