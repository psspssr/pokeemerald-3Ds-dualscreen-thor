#!/usr/bin/env python3
"""Import an upstream release while keeping origin/ an exact, unedited subtree.

  python3 tools/sync_origin.py --dry-run
  python3 tools/sync_origin.py
  python3 tools/sync_origin.py --ref <branch|tag|full-commit>

An update creates local Git commits. It never pushes, resets or cleans the
development checkout. A temporary worktree receives the subtree merge and
lock change; only a completed, verified import advances the local branch.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid

from check_origin import ROOT, OriginError, check_origin, fetch_ref, git, write_lock


def require_clean(root: Path) -> str:
    status = git(root, "status", "--porcelain=v1", "--untracked-files=all").stdout
    if status:
        raise OriginError("commit or stash local changes before importing upstream:\n" + status)
    branch = git(root, "symbolic-ref", "--quiet", "--short", "HEAD", check=False)
    if branch.returncode:
        raise OriginError("check out a local branch before importing upstream")
    return git(root, "rev-parse", "HEAD").stdout.strip()


def check_ref(root: Path, ref: str) -> None:
    # Ref names are fetch arguments, never arbitrary refspecs or options.
    if (ref.startswith("-") or ":" in ref or
            git(root, "check-ref-format", "--allow-onelevel", ref, check=False).returncode):
        raise OriginError("--ref must be a branch, tag or full commit ID, not a refspec")


def report_changes(root: Path, old: str, new: str) -> None:
    print(f"origin: {old[:12]} -> {new[:12]}")
    print(git(root, "log", "--format=%h %s", f"{old}..{new}").stdout.strip() or "(no added commits)")
    print(git(root, "diff", "--stat", old, new).stdout.strip() or "(identical source trees)")
    names = git(root, "diff", "--name-only", old, new).stdout.splitlines()
    integration = [p for p in names if p.startswith(("3ds_port/", "tools/", "patches/"))
                   or p == "upstream.lock"]
    if integration:
        print("Build and platform inputs to review:")
        print("\n".join("  " + p for p in integration))


def synchronize(root: Path, *, ref: str | None = None, dry_run: bool = False,
                allow_rewind: bool = False) -> int:
    before = None if dry_run else require_clean(root)
    lock = check_origin(root)
    requested = ref or lock["branch"]
    check_ref(root, requested)
    namespace = f"refs/emerald-origin-update/{uuid.uuid4().hex}"
    refs = [namespace + "/old", namespace + "/new"]
    try:
        old = fetch_ref(root, lock["repository"], lock["commit"], refs[0])
        if git(root, "rev-parse", f"{old}^{{tree}}").stdout.strip() != lock["tree"]:
            raise OriginError("origin.lock tree does not match the pinned upstream commit")
        new = fetch_ref(root, lock["repository"], requested, refs[1])
        if old == new:
            print(f"origin: already at {new}; nothing changed")
            return 0
        ancestry = git(root, "merge-base", "--is-ancestor", old, new, check=False)
        if ancestry.returncode not in (0, 1):
            raise OriginError("could not establish upstream commit ancestry")
        if ancestry.returncode and not allow_rewind:
            raise OriginError("requested ref is not a descendant of the current pin; "
                              "inspect it, then use --allow-rewind if intentional")
        report_changes(root, old, new)
        if dry_run:
            print("Preview only; checkout, index, branch, and origin.lock are unchanged.")
            return 0

        with tempfile.TemporaryDirectory(prefix="emerald-origin-update-") as temporary:
            staging = Path(temporary) / "tree"
            git(root, "worktree", "add", "--detach", str(staging), before)
            try:
                git(staging, "subtree", "merge", "--prefix=origin", new, "--squash",
                    "-m", f"Import upstream origin {new[:12]}")
                expected_tree = git(root, "rev-parse", f"{new}^{{tree}}").stdout.strip()
                actual_tree = git(staging, "rev-parse", "HEAD:origin").stdout.strip()
                if actual_tree != expected_tree:
                    raise OriginError("subtree merge did not produce the exact upstream tree; "
                                      "development checkout was not changed")
                updated = {**lock, "commit": new, "tree": expected_tree}
                write_lock(staging, updated)
                git(staging, "add", "--", "origin.lock")
                git(staging, "commit", "-m", f"Pin origin at {new[:12]}")
                check_origin(staging)
                # Old build outputs do not describe the new sources. The
                # header check is advisory; a full build remains mandatory.
                coverage = subprocess.run(
                    [sys.executable, str(staging / "tools/check_shim_coverage.py"), "--headers-only"],
                    cwd=staging, check=False)
                if coverage.returncode not in (0, 1):
                    raise OriginError("shim coverage check could not run; development checkout was not changed")
                if require_clean(root) != before:
                    raise OriginError("development branch changed during the import; rerun after reviewing it")
                completed = git(staging, "rev-parse", "HEAD").stdout.strip()
                git(root, "merge", "--ff-only", completed)
                check_origin(root)
                print(f"Imported {new} and updated origin.lock on the current local branch.")
                print("Next: python3 tools/bootstrap.py --make --apk")
                if coverage.returncode:
                    print("The import succeeded; missing SDK declarations require Android layer work.")
                return coverage.returncode
            finally:
                # Only this script's disposable worktree is removed. Never
                # reset/clean the caller's checkout or build/upstream.
                git(root, "worktree", "remove", "--force", str(staging))
    finally:
        for temporary_ref in refs:
            git(root, "update-ref", "-d", temporary_ref, check=False)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true", help="fetch and report without changing the checkout")
    parser.add_argument("--ref", help="upstream branch, tag or full commit (default: origin.lock branch)")
    parser.add_argument("--allow-rewind", action="store_true", help="explicitly allow a downgrade or divergent ref")
    args = parser.parse_args()
    try:
        return synchronize(ROOT, ref=args.ref, dry_run=args.dry_run, allow_rewind=args.allow_rewind)
    except (OriginError, OSError) as exc:
        print(f"sync_origin: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
