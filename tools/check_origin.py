#!/usr/bin/env python3
"""Verify the immutable origin subtree; --fetch also checks the upstream pin.

The offline check compares all tracked paths, file modes and contents with
the tree recorded in origin.lock. The network check additionally verifies
that the upstream commit has that exact tree. Neither updates origin/.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tomllib
import uuid

ROOT = Path(__file__).resolve().parents[1]


class OriginError(RuntimeError):
    pass


def git(root: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(["git", *args], cwd=root, text=True, capture_output=True)
    if check and result.returncode:
        raise OriginError(f"git {' '.join(args)} failed:\n{result.stderr.strip() or result.stdout.strip()}")
    return result


def read_lock(root: Path) -> dict[str, str]:
    try:
        data = tomllib.loads((root / "origin.lock").read_text(encoding="utf-8"))["origin"]
        for name in ("repository", "branch", "commit", "tree"):
            if not isinstance(data[name], str) or not data[name] or "\n" in data[name]:
                raise ValueError(f"invalid origin.{name}")
        for name in ("commit", "tree"):
            if not re.fullmatch(r"[0-9a-f]{40}", data[name]):
                raise ValueError(f"origin.{name} must be a full Git SHA-1")
        if data["repository"].startswith("-"):
            raise ValueError("invalid origin.repository")
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise OriginError(f"invalid origin.lock: {exc}") from exc
    return data


def write_lock(root: Path, lock: dict[str, str]) -> None:
    text = ("# The upstream commit imported unchanged into origin/ (squashed git subtree).\n"
            "# Updated by tools/sync_origin.py; tree is checked by tools/check_origin.py.\n[origin]\n")
    text += "".join(f"{name} = {json.dumps(lock[name])}\n"
                    for name in ("repository", "branch", "commit", "tree"))
    (root / "origin.lock").write_text(text, encoding="utf-8")


def fetch_ref(root: Path, repository: str, ref: str, destination: str) -> str:
    # No tags, remotes or FETCH_HEAD are changed. A caller-owned reference
    # keeps the fetched object alive only for the duration of verification.
    git(root, "fetch", "--no-tags", "--no-write-fetch-head", "--", repository,
        f"{ref}:{destination}")
    return git(root, "rev-parse", f"{destination}^{{commit}}").stdout.strip()


def check_origin(root: Path, *, fetch: bool = False) -> dict[str, str]:
    lock = read_lock(root)
    dirty = git(root, "status", "--porcelain=v1", "--untracked-files=all", "--", "origin").stdout
    if dirty:
        raise OriginError(f"origin/ contains local changes; preserve or move them before continuing:\n{dirty}")
    tree = git(root, "rev-parse", "HEAD:origin").stdout.strip()
    if tree != lock["tree"]:
        raise OriginError(f"origin/ tree {tree} differs from origin.lock tree {lock['tree']}")
    if fetch:
        ref = f"refs/emerald-origin-check/{uuid.uuid4().hex}"
        try:
            commit = fetch_ref(root, lock["repository"], lock["commit"], ref)
            upstream_tree = git(root, "rev-parse", f"{commit}^{{tree}}").stdout.strip()
            if commit != lock["commit"] or upstream_tree != tree:
                raise OriginError("origin/ does not match the pinned upstream commit")
        finally:
            git(root, "update-ref", "-d", ref, check=False)
    return lock


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true", help="fetch and verify the exact upstream commit")
    args = parser.parse_args()
    try:
        lock = check_origin(ROOT, fetch=args.fetch)
    except OriginError as exc:
        print(f"check_origin: {exc}", file=sys.stderr)
        return 1
    source = "upstream commit" if args.fetch else "recorded tree (offline)"
    print(f"check_origin: origin/ matches {source} {lock['commit'][:12]} ({lock['tree']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
