# Dedicated repository history

On 2026-10-05, the repository history was trimmed to work committed after this
dedicated repository was created on **2026-10-02 at 09:33:25 UTC**. Eighteen
earlier commits were removed. All 137 later commits were retained, including
their source trees, authors, dates, messages and remaining merge structure.
Both `main` and `dev`, and all nine existing release tags, use that history.

Commit IDs changed because their parent chains changed. Every retained source
snapshot is byte-for-byte identical. The imported upstream source and its
`git-subtree-split` revisions were preserved.

## Existing releases

Published APKs, build manifests, checksums, release IDs and download URLs were
preserved. The `code_commit` values in those manifests describe the original
build inputs and must not be edited to imply a different build.

The [source mapping](history/2026-10-05-source-map.json) gives the corresponding
rewritten commits and identical Git tree IDs. It also distinguishes alpha.1's
build source from its later documentation-only release commit. Validation and
historical CI records may therefore refer to original commit IDs.

Old release-workflow attempts retain their original event/source bindings and
are historical records after this rewrite. Do not republish an old release or
weaken its integrity checks to rerun it. Use a new tag for a future build, with
the existing app ID, signing identity and increasing Android version code.

## Existing clones and upstream updates

A fresh clone is the simplest way to get the cleaned history. Preserve any
uncommitted work first. Do not merge an old branch into the new history: that
can restore the removed ancestry. Reapply wanted changes onto current `main`
or `dev` instead.

Run upstream updates from current `main` or `dev`; their subtree metadata was
retained and the updater was checked after the rewrite. The oldest release
tags retain their build files, but their original pre-repository subtree marker
was removed with that earlier ancestry.

Rewriting advertised branches and tags does not erase existing clones or
GitHub's cached views by old commit ID. See [GitHub's documented limits](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository).
