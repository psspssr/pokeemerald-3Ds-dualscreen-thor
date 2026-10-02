"""Real Git/patch regressions without network, Android tools or game content."""

from contextlib import redirect_stdout
import difflib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import bootstrap

ORIGIN_BOOTSTRAP = bootstrap.ORIGIN / "tools/bootstrap.py"
BASE = "int before;\nint value = 0;\nint after;\n"
PORT_BASE = "int before;\nint view = 0;\nint after;\n"


def text_patch(rel, before, after):
    header = "diff --git a/%s b/%s\n" % (rel, rel)
    if before is None:
        header += "new file mode 100644\n"
    if after is None:
        header += "deleted file mode 100644\n"
    return header + "".join(difflib.unified_diff(
        [] if before is None else before.splitlines(keepends=True),
        [] if after is None else after.splitlines(keepends=True),
        fromfile="/dev/null" if before is None else "a/" + rel,
        tofile="/dev/null" if after is None else "b/" + rel))


def command(args, cwd=None, env=None):
    return subprocess.run([str(arg) for arg in args], cwd=cwd, env=env,
                          check=True, capture_output=True, text=True)


class AndroidPatchTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="emerald-patch-test-")
        self.addCleanup(temporary.cleanup)
        base = Path(temporary.name)
        self.root = base / "port"
        self.origin = self.root / "origin"
        self.patches = self.root / "patches/android"
        self.tree = self.root / "build/upstream"
        self.pret = base / "pret"
        self.write(self.pret / "src/game.c", BASE)
        self.write(self.pret / "src/obsolete.c", "int obsolete;\n")
        self.write(self.pret / "script.sh", "#!/bin/sh\nexit 0\n")
        command(["git", "init", "-q", self.pret])
        command(["git", "add", "."], self.pret)
        command(["git", "-c", "user.name=Patch Test", "-c", "user.email=test@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "-qm", "base"], self.pret)
        commit = command(["git", "rev-parse", "HEAD"], self.pret).stdout.strip()
        self.write(self.origin / "upstream.lock",
                   '[pokeemerald]\nrepository = %s\ncommit = "%s"\n'
                   % (json.dumps(str(self.pret)), commit))
        self.write(self.origin / "3ds_port/src/view.c", PORT_BASE)
        self.write(self.origin / "patches/pokeemerald/001-base.patch",
                   text_patch("src/game.c", BASE, BASE.replace("= 0", "= 1")))
        (self.origin / "tools").mkdir(parents=True)
        shutil.copy2(ORIGIN_BOOTSTRAP, self.origin / "tools/bootstrap.py")
        self.patches.mkdir(parents=True)
        self.origin_id = "first-origin-pin"
        for key, value in {"ROOT": self.root, "ORIGIN": self.origin,
                           "ANDROID": self.root / "android", "PATCHES": self.patches}.items():
            change = patch.object(bootstrap, key, value)
            change.start()
            self.addCleanup(change.stop)

    def write(self, path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def add_patch(self, name, *changes):
        return self.write(self.patches / name, "".join(text_patch(*change) for change in changes))

    def bootstrap(self, *args):
        with patch.object(sys, "argv", ["bootstrap.py", "--dir", str(self.tree), *args]), \
                patch.object(bootstrap, "check_origin", return_value={"commit": self.origin_id}), \
                patch.object(bootstrap, "run", side_effect=command), redirect_stdout(io.StringIO()):
            self.assertEqual(0, bootstrap.main())

    def state(self):
        return json.loads((self.tree / bootstrap.MARKER).read_text())

    def test_ordered_overlapping_patches_repeat_without_source_or_build_changes(self):
        self.add_patch("010-first.patch", ("src/game.c", BASE.replace("= 0", "= 1"),
                                          BASE.replace("= 0", "= 2")),
                       ("3ds_port/src/view.c", PORT_BASE, PORT_BASE.replace("= 0", "= 1")))
        self.add_patch("020-second.patch", ("src/game.c", BASE.replace("= 0", "= 2"),
                                           BASE.replace("= 0", "= 3")))
        originals = {path: path.read_bytes() for path in self.origin.rglob("*") if path.is_file()}
        self.bootstrap()
        self.assertEqual(BASE.replace("= 0", "= 3"), (self.tree / "src/game.c").read_text())
        paths = ("src/game.c", "3ds_port/src/view.c")
        times = {rel: (self.tree / rel).stat().st_mtime_ns for rel in paths}
        self.write(self.tree / "3ds_port/build/config", "warm configuration")
        self.write(self.tree / "3ds_port/build/keep.o", "keep compiled product")
        state = self.state()
        self.bootstrap()
        self.assertEqual(state, self.state())
        self.assertEqual(times, {rel: (self.tree / rel).stat().st_mtime_ns for rel in paths})
        self.assertEqual("warm configuration", (self.tree / "3ds_port/build/config").read_text())
        self.assertEqual("keep compiled product", (self.tree / "3ds_port/build/keep.o").read_text())
        self.assertEqual(originals, {path: path.read_bytes() for path in originals})

    def test_changed_and_removed_series_refresh_sources_and_configuration(self):
        hook = self.add_patch("020-hooks.patch", ("src/game.c", BASE.replace("= 0", "= 1"),
                                                 BASE.replace("= 0", "= 2")),
                              ("include/hook.h", None, "int hook(void);\n"))
        self.bootstrap()
        self.write(self.tree / "3ds_port/build/config", "old configuration")
        self.write(self.tree / "3ds_port/build/keep.o", "keep compiled product")
        self.add_patch(hook.name, ("src/game.c", BASE.replace("= 0", "= 1"),
                                  BASE.replace("= 0", "= 4")))
        self.bootstrap()
        self.assertEqual(BASE.replace("= 0", "= 4"), (self.tree / "src/game.c").read_text())
        self.assertFalse((self.tree / "include/hook.h").exists())
        self.assertFalse((self.tree / "3ds_port/build/config").exists())
        self.assertTrue((self.tree / "3ds_port/build/keep.o").exists())
        hook.unlink()
        self.bootstrap()
        self.assertEqual(BASE.replace("= 0", "= 1"), (self.tree / "src/game.c").read_text())
        self.assertEqual({}, self.state()["files"])

    def test_origin_revision_change_rechecks_context_and_invalidates_configuration(self):
        self.add_patch("010-view.patch", ("3ds_port/src/view.c", PORT_BASE,
                                         PORT_BASE.replace("= 0", "= 1")))
        self.bootstrap()
        self.write(self.tree / "3ds_port/build/config", "old")
        self.origin_id = "next-origin-pin"
        self.bootstrap()
        self.assertFalse((self.tree / "3ds_port/build/config").exists())
        self.origin_id = "incompatible-origin-pin"
        incompatible = PORT_BASE.replace("int before;", "int upstream_changed;")
        self.write(self.origin / "3ds_port/src/view.c", incompatible)
        with self.assertRaisesRegex(bootstrap.PatchError, "rebase the patch"):
            self.bootstrap()
        self.assertEqual(incompatible, (self.tree / "3ds_port/src/view.c").read_text())
        self.assertFalse((self.tree / bootstrap.MARKER).exists())

    def test_late_patch_failure_does_not_partially_modify_sources(self):
        self.bootstrap()
        first = self.add_patch("010-ok.patch", ("src/game.c", BASE.replace("= 0", "= 1"),
                                               BASE.replace("= 0", "= 2")))
        second = self.add_patch("020-wrong.patch", ("3ds_port/src/view.c", "wrong base\n", "wrong result\n"))
        with self.assertRaisesRegex(bootstrap.PatchError, "020-wrong.patch"):
            bootstrap.apply_patches(self.tree, [first, second])
        self.assertEqual(BASE.replace("= 0", "= 1"), (self.tree / "src/game.c").read_text())
        self.assertEqual(PORT_BASE, (self.tree / "3ds_port/src/view.c").read_text())

    def test_plain_unified_patch_accounts_for_every_file(self):
        self.bootstrap()
        hook = self.add_patch("010-unified.patch", ("src/game.c", BASE.replace("= 0", "= 1"),
                                                    BASE.replace("= 0", "= 2")),
                              ("3ds_port/src/view.c", PORT_BASE, PORT_BASE.replace("= 0", "= 1")))
        hook.write_text("".join(line for line in hook.read_text().splitlines(keepends=True)
                                if not line.startswith("diff --git ")))
        images = bootstrap.apply_patches(self.tree, [hook])
        self.assertEqual({"src/game.c", "3ds_port/src/view.c"}, set(images))
        self.assertEqual(PORT_BASE.replace("= 0", "= 1"), (self.tree / "3ds_port/src/view.c").read_text())

    def test_already_applied_source_without_provenance_is_not_silently_accepted(self):
        self.bootstrap()
        changed = BASE.replace("= 0", "= 2")
        self.write(self.tree / "src/game.c", changed)
        hook = self.add_patch("010-unknown.patch", ("src/game.c", BASE.replace("= 0", "= 1"), changed))
        with self.assertRaisesRegex(bootstrap.PatchError, "rebase the patch"):
            bootstrap.apply_patches(self.tree, [hook])
        self.assertEqual(changed, (self.tree / "src/game.c").read_text())

    def test_shifted_hunk_is_rejected_instead_of_silent_offset(self):
        self.bootstrap()
        before = "".join("line %d\n" % n for n in range(20))
        shifted = "extra line\n" + before
        self.write(self.tree / "src/offset.c", shifted)
        hook = self.add_patch("010-offset.patch", ("src/offset.c", before,
                                                  before.replace("line 10\n", "changed line\n")))
        with self.assertRaisesRegex(bootstrap.PatchError, "offset 1 line"):
            bootstrap.apply_patches(self.tree, [hook])
        self.assertEqual(shifted, (self.tree / "src/offset.c").read_text())

    def test_local_source_edit_is_preserved_and_clean_is_explicit_recovery(self):
        self.add_patch("010-view.patch", ("3ds_port/src/view.c", PORT_BASE,
                                         PORT_BASE.replace("= 0", "= 1")))
        self.bootstrap()
        self.write(self.tree / "3ds_port/src/view.c", "local diagnostic edit\n")
        with self.assertRaisesRegex(bootstrap.PatchError, "preserve any edits"):
            self.bootstrap()
        self.assertEqual("local diagnostic edit\n", (self.tree / "3ds_port/src/view.c").read_text())
        self.bootstrap("--clean")
        self.assertEqual(PORT_BASE.replace("= 0", "= 1"), (self.tree / "3ds_port/src/view.c").read_text())

    def test_additions_deletions_and_executable_modes_are_repeatable(self):
        hook = self.add_patch("010-files.patch", ("include/hook.h", None, "int hook(void);\n"),
                              ("src/obsolete.c", "int obsolete;\n", None))
        hook.write_text(hook.read_text() + "diff --git a/script.sh b/script.sh\nold mode 100644\nnew mode 100755\n")
        self.bootstrap()
        self.bootstrap()
        self.assertFalse((self.tree / "src/obsolete.c").exists())
        self.assertEqual("int hook(void);\n", (self.tree / "include/hook.h").read_text())
        self.assertTrue((self.tree / "script.sh").stat().st_mode & 0o100)

    def test_unsafe_paths_symlinks_and_nontext_changes_fail_closed(self):
        self.bootstrap()
        for rel in ("../escaped.c", "/absolute.c", "3ds_port/build/config", "android/native/src/hook.c"):
            hook = self.add_patch("010-unsafe.patch", (rel, None, "bad\n"))
            with self.subTest(rel=rel), self.assertRaises(bootstrap.PatchError):
                bootstrap.apply_patches(self.tree, [hook])
        outside = self.write(self.root / "outside.txt", "unchanged\n")
        (self.tree / "src/link.c").symlink_to(outside)
        hook = self.add_patch("010-unsafe.patch", ("src/link.c", "unchanged\n", "changed\n"))
        with self.assertRaisesRegex(bootstrap.PatchError, "symlink"):
            bootstrap.apply_patches(self.tree, [hook])
        self.assertEqual("unchanged\n", outside.read_text())
        hook.write_text("diff --git a/link b/link\nnew file mode 120000\n")
        with self.assertRaisesRegex(bootstrap.PatchError, "symlink/submodule"):
            bootstrap.patch_files(hook)
        hook.write_text("diff --git a/data.bin b/data.bin\nGIT binary patch\n")
        with self.assertRaisesRegex(bootstrap.PatchError, "regular-file text"):
            bootstrap.patch_files(hook)

    def test_corrupt_or_nonempty_legacy_state_requires_clean(self):
        self.bootstrap()
        marker = self.tree / bootstrap.MARKER
        for value in ("broken json", json.dumps({"digest": "old", "created": ["src/hook.c"]})):
            marker.write_text(value)
            with self.assertRaisesRegex(bootstrap.PatchError, "--clean"):
                self.bootstrap()
        self.bootstrap("--clean")
        self.assertEqual(bootstrap.MARKER_VERSION, self.state()["version"])

    def test_empty_legacy_marker_upgrades_without_discarding_build_outputs(self):
        self.bootstrap()
        self.write(self.tree / bootstrap.MARKER,
                   json.dumps({"digest": bootstrap.digest([]), "created": []}))
        self.write(self.tree / "3ds_port/build/keep.o", "preserved")
        self.bootstrap()
        self.assertEqual("preserved", (self.tree / "3ds_port/build/keep.o").read_text())
        self.assertEqual(bootstrap.MARKER_VERSION, self.state()["version"])


if __name__ == "__main__":
    unittest.main()
