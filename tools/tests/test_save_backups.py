"""Exercise the production native backup writer with real files and I/O faults."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SaveBackupTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="emerald-backup-build-")
        cls.addClassCleanup(cls.build.cleanup)
        cls.executable = Path(cls.build.name) / "save-backups"
        result = subprocess.run([
            os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I" + str(ROOT / "android/native/include"), "-I" + str(ROOT / "android/host/include"),
            str(ROOT / "android/native/test/test_save_backups.c"),
            str(ROOT / "android/native/src/qol_backups.c"),
            "-Wl,--wrap=write", "-Wl,--wrap=renameat", "-Wl,--wrap=fsync", "-Wl,--wrap=clock_gettime",
            "-o", str(cls.executable)
        ], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def check_case(self, name):
        with tempfile.TemporaryDirectory(prefix="emerald-save-backups-") as folder:
            result = subprocess.run([str(self.executable), name, folder],
                                    capture_output=True, text=True, timeout=10)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_disabled_does_no_backup_io(self):
        self.check_case("disabled")

    def test_completed_slot_exact_snapshot_and_same_millisecond(self):
        self.check_case("complete")

    def test_corruption_partial_and_older_slot_are_rejected(self):
        self.check_case("invalid")

    def test_keep_five_after_import_counter_reset_and_clock_rollback(self):
        self.check_case("retention")

    def test_partial_write_failure_preserves_save_and_existing_backups(self):
        self.check_case("write")

    def test_failed_rename_preserves_save_and_existing_backups(self):
        self.check_case("rename")

    def test_failed_fsync_preserves_save_and_existing_backups(self):
        self.check_case("sync")
