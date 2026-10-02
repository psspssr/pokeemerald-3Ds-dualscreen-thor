# Updating the upstream 3DS project

`origin/` is an unmodified squashed Git subtree of
[ZallaxDev/pokeemerald-3Ds-dualscreen](https://github.com/ZallaxDev/pokeemerald-3Ds-dualscreen).
The remote named `origin` belongs to **this Android repository**; the imported
project's URL, default branch, exact commit and tree hash live in `origin.lock`.
Android fixes belong outside `origin/`.

## Preview and import

From the repository root:

```sh
python3 tools/sync_origin.py --dry-run
python3 tools/sync_origin.py
```

The updater fetches the upstream branch recorded in `origin.lock` (`main` by
default). To select a specific release, use a branch, tag or full commit:

```sh
python3 tools/sync_origin.py --ref <tag-or-commit> --dry-run
python3 tools/sync_origin.py --ref <tag-or-commit>
```

`--ref` selects this import only; it does not change the default branch stored
in the lock. A downgrade or divergent history is refused unless
`--allow-rewind` is explicitly supplied.

An import requires a local branch and a clean checkout, including staged and
untracked files. Commit or stash your changes first. Git author identity must
be configured because the import creates local commits. The script:

1. Verifies that `origin/` matches both the lock's tree and the exact upstream
   commit. Local modifications or a mismatched pin stop the import.
2. Fetches and reports upstream commits and changed build/platform inputs.
3. Merges the squashed subtree in a disposable Git worktree, verifies the
   resulting tree byte-for-byte including file modes, and commits `origin.lock`.
4. Runs the SDK header coverage check against the new sources.
5. Fast-forwards the development branch only if it is still clean and at the
   same commit as when the import started.

It never pushes or runs `reset`/`clean` on your checkout. A failed fetch,
subtree merge, tree check, or checker execution leaves the checkout unchanged.
`--dry-run` only fetches and reports; it does not update the checkout, index,
branch or lock. Temporary fetch references and the temporary worktree are
removed after the operation.

If the import introduces SDK names absent from the Android headers, the import
is retained and the command exits **1** after reporting the names. Implement
those APIs in `android/shim` or `android/gpu`, then build. Re-running the updater
at the same pin makes no new commits; always read whether the preceding run
reported a completed import or stopped before advancing the branch.

## Rebuild and check

The usual one-line update and rebuild is:

```sh
python3 tools/sync_origin.py && python3 tools/bootstrap.py --make --apk -j4
```

Then run the checks in [BUILDING.md](BUILDING.md), including real-game runtime
acceptance. A successful import does not prove that new upstream behavior is
implemented by the Android SDK layer. Regenerate the Android data pack along
with an engine-only build because executable pointers and the pack ABI may
change. Export saves before installing a changed build.

| Upstream change | Integration check |
|---|---|
| Game code, patches, source lists, generators, or pinned pret commit | Bootstrap and rebuild; origin's own source lists and rules remain authoritative. Review changes to generators and build recipes. |
| New libctru, Citro3D or Citro2D APIs | Header check reports likely gaps; native compile and strict link verify declarations and definitions. Implement semantics and add relevant tests. |
| New PICA shader instructions | The translator must support the opcode; verify compiled GLSL and rendered output. A successful translation alone does not prove visual parity. |
| Changes to link layout, script relocation or `.gamedata` | Review the Android link and fixed-address loader; both ELF images must agree on addresses. |
| Removed or renamed source/assets | Ensure the regenerated tree and package contain the new source set without stale outputs. |

If Clang rejects an upstream pattern accepted by devkitARM GCC, prefer a
compiler flag or forced-include adapter in `android/toolchain/` or
`android/native/compat/`. Keep any unavoidable patch in `patches/android/`
with its reason. Do not fix it by editing the imported tree.

## Integrity and tooling regression checks

```sh
python3 tools/check_origin.py          # local tree versus recorded hash
python3 tools/check_origin.py --fetch  # also verify the actual upstream commit
python3 -m unittest discover -s tools/tests -v
python3 tools/check_shim_coverage.py --headers-only
python3 tools/check_shim_coverage.py   # after the native build, with NDK available
```

The source coverage scan is deliberately advisory and follows upstream's
native Makefile entries and local headers. It fails explicitly if it cannot
understand a source-list change. CI runs the exact-pin integrity check, the
local-Git updater tests, a complete native build, and Android harness tests.
