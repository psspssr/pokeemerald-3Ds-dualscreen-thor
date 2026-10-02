"""Updater integration tests use real local Git repos and no network/game data."""

from contextlib import redirect_stdout
import io
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
from check_origin import OriginError, check_origin, git, read_lock, write_lock
from sync_origin import synchronize


class OriginUpdateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="emerald-updater-test-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.upstream = self.base / "upstream"
        self.repo = self.base / "android"
        for repo in (self.upstream, self.repo):
            repo.mkdir()
            git(repo, "init", "-q", "-b", "main")
            git(repo, "config", "user.name", "Updater Test")
            git(repo, "config", "user.email", "updater-test@example.invalid")
            git(repo, "config", "commit.gpgsign", "false")
        (self.upstream / "source.c").write_text("int old_version;\n")
        self.old = self.commit(self.upstream, "first version")
        (self.repo / "README").write_text("Android integration\n")
        (self.repo / ".gitignore").write_text("__pycache__/\n")
        (self.repo / "tools").mkdir()
        # A controlled return code isolates transactional update behavior from
        # SDK parsing (covered separately in test_shim_coverage.py).
        (self.repo / "tools/check_shim_coverage.py").write_text("raise SystemExit(0)\n")
        self.commit(self.repo, "Android tree")
        git(self.repo, "subtree", "add", "--prefix=origin", str(self.upstream), self.old, "--squash")
        write_lock(self.repo, {"repository": str(self.upstream), "branch": "main", "commit": self.old,
                              "tree": git(self.upstream, "rev-parse", "HEAD^{tree}").stdout.strip()})
        self.commit(self.repo, "pin source")
        self.before = git(self.repo, "rev-parse", "HEAD").stdout.strip()
        (self.upstream / "source.c").write_text("int new_version;\n")
        (self.upstream / "added.bin").write_bytes(bytes(range(256)))
        script = self.upstream / "run.sh"
        script.write_text("#!/bin/sh\nexit 0\n")
        script.chmod(0o755)
        (self.upstream / "source-link").symlink_to("source.c")
        self.new = self.commit(self.upstream, "new version with binary, executable and symlink")

    def commit(self, repo, message):
        git(repo, "add", ".")
        git(repo, "commit", "-qm", message)
        return git(repo, "rev-parse", "HEAD").stdout.strip()

    def sync(self, **kwargs):
        with redirect_stdout(io.StringIO()):
            return synchronize(self.repo, **kwargs)

    def assert_original_checkout(self):
        self.assertEqual(self.before, git(self.repo, "rev-parse", "HEAD").stdout.strip())
        self.assertEqual(self.old, read_lock(self.repo)["commit"])
        self.assertEqual("int old_version;\n", (self.repo / "origin/source.c").read_text())

    def assert_no_temporary_refs_or_worktrees(self):
        self.assertEqual("", git(self.repo, "for-each-ref", "refs/emerald-origin-update").stdout)
        self.assertEqual(1, git(self.repo, "worktree", "list", "--porcelain").stdout.count("worktree "))

    def test_dry_run_does_not_change_checkout_or_index(self):
        self.assertEqual(0, self.sync(dry_run=True))
        self.assert_original_checkout()
        self.assertEqual("", git(self.repo, "status", "--porcelain").stdout)
        self.assert_no_temporary_refs_or_worktrees()

    def test_update_exact_tree_is_repeatable(self):
        self.assertEqual(0, self.sync())
        self.assertEqual(self.new, check_origin(self.repo, fetch=True)["commit"])
        self.assertEqual(git(self.upstream, "rev-parse", "HEAD^{tree}").stdout.strip(),
                         git(self.repo, "rev-parse", "HEAD:origin").stdout.strip())
        after = git(self.repo, "rev-parse", "HEAD").stdout
        self.assertEqual(0, self.sync())
        self.assertEqual(after, git(self.repo, "rev-parse", "HEAD").stdout)
        self.assertEqual("", git(self.repo, "status", "--porcelain").stdout)
        self.assert_no_temporary_refs_or_worktrees()

    def test_dirty_worktree_and_untracked_files_are_preserved(self):
        for path in (self.repo / "README", self.repo / "untracked.txt"):
            original = path.read_bytes() if path.exists() else None
            path.write_text("Do not discard my work\n")
            with self.assertRaisesRegex(OriginError, "commit or stash"):
                self.sync()
            self.assert_original_checkout()
            self.assertEqual("Do not discard my work\n", path.read_text())
            if original is None:
                path.unlink()
            else:
                path.write_bytes(original)

    def test_staged_change_is_preserved(self):
        (self.repo / "README").write_text("staged user change\n")
        git(self.repo, "add", "README")
        before = git(self.repo, "diff", "--cached").stdout
        with self.assertRaisesRegex(OriginError, "commit or stash"):
            self.sync()
        self.assertEqual(before, git(self.repo, "diff", "--cached").stdout)

    def test_origin_drift_is_rejected_even_for_preview(self):
        (self.repo / "origin/source.c").write_text("int android_fork;\n")
        with self.assertRaisesRegex(OriginError, "origin/ contains local changes"):
            self.sync(dry_run=True)

    def test_forged_pin_tree_is_rejected(self):
        lock = read_lock(self.repo)
        lock["commit"] = self.new
        write_lock(self.repo, lock)
        self.before = self.commit(self.repo, "wrong commit in lock")
        with self.assertRaisesRegex(OriginError, "does not match"):
            self.sync()
        self.assertEqual(self.before, git(self.repo, "rev-parse", "HEAD").stdout.strip())

    def test_failed_fetch_leaves_checkout_untouched(self):
        with self.assertRaises(OriginError):
            self.sync(ref="missing-branch")
        self.assert_original_checkout()
        self.assert_no_temporary_refs_or_worktrees()

    def test_failed_check_leaves_checkout_untouched(self):
        (self.repo / "tools/check_shim_coverage.py").write_text("raise SystemExit(2)\n")
        self.before = self.commit(self.repo, "checker cannot run")
        with self.assertRaisesRegex(OriginError, "could not run"):
            self.sync()
        self.assert_original_checkout()
        self.assert_no_temporary_refs_or_worktrees()

    def test_missing_declarations_are_reported_after_successful_import(self):
        (self.repo / "tools/check_shim_coverage.py").write_text("raise SystemExit(1)\n")
        self.commit(self.repo, "checker reports a new SDK symbol")
        self.assertEqual(1, self.sync())
        self.assertEqual(self.new, check_origin(self.repo)["commit"])
        self.assert_no_temporary_refs_or_worktrees()

    def test_downgrade_requires_explicit_flag(self):
        self.assertEqual(0, self.sync())
        with self.assertRaisesRegex(OriginError, "not a descendant"):
            self.sync(ref=self.old)
        self.assertEqual(self.new, read_lock(self.repo)["commit"])
        self.assertEqual(0, self.sync(ref=self.old, allow_rewind=True))
        self.assertEqual(self.old, check_origin(self.repo, fetch=True)["commit"])

    def test_refspec_and_option_injection_are_rejected(self):
        for ref in ("main:refs/heads/other", "--all", "main\nmain"):
            with self.subTest(ref=ref), self.assertRaisesRegex(OriginError, "--ref must be"):
                self.sync(ref=ref)
        self.assert_original_checkout()

    def test_detached_head_is_not_updated(self):
        git(self.repo, "checkout", "--detach", "HEAD")
        with self.assertRaisesRegex(OriginError, "local branch"):
            self.sync()
        self.assert_original_checkout()


if __name__ == "__main__":
    unittest.main()
