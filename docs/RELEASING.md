# Private Android releases

Publish a GitHub release to start [the release workflow](../.github/workflows/release.yml).
It validates the tagged source, builds the playable ARMv7 app, signs it with the
existing release key, and attaches the APK, `build-info.json` and `SHA256SUMS`
to that same release ID. Repository visibility remains **private**.
Routine pushes and pull requests run [normal validation](../.github/workflows/android.yml);
they do not run the signing or release workflow, or publish APKs.

The release build runs bootstrap **without** `--release`, then Gradle
`assembleRelease lintRelease`, so the APK includes matching game data and
starts immediately. Bootstrap's `--release` means **engine-only data packaging**;
it is separate from Gradle's release build type. Public distribution remains
a separate packaging workflow described in
[BUILDING.md](BUILDING.md#engine-only-build-and-data-packs).

## One-time signing setup

The repository owner configures these two GitHub Actions repository secrets:

| Secret | Value |
|---|---|
| `ANDROID_RELEASE_KEYSTORE_B64` | Base64 encoding of the existing release keystore. |
| `ANDROID_RELEASE_KEYSTORE_PASSWORD` | Its password, also used for the private key. |

Keep alias `emerald-thor` and the established signing certificate SHA-256:
`eeb95f89fcb944d3a62cc2aa8d0bb720584333d476c13b5a823ef486fdcd0389`.
The owner-managed originals are
`/root/.local/share/emerald-thor/signing/release.keystore` and `release.pass`
in that directory. Maintain a separate owner-controlled backup with restricted
permissions. Never print or commit either secret, or replace the signing key
for a normal update. Android updates require the same signing identity.

Only the isolated signing job's signing step receives the secrets. That job
has no source checkout and executes no build scripts from its input artifact.
It verifies the unsigned APK's identity before signing, checks the resulting
certificate and alignment, and deletes its temporary key files. No PAT is
needed; the publisher alone gets `contents: write` on its `GITHUB_TOKEN`.

## Publish a reviewed source commit

Use a strict SemVer tag, for example `v0.1.0-alpha.2`. The workflow derives
Android `versionName` by removing `v`, and assigns `versionCode` as its
`GITHUB_RUN_NUMBER + 1`. Code **1** belongs to the first manual release,
`v0.1.0-alpha.1`; the first automated run uses code **2**. New runs increase
the code; reruns keep it unchanged. Do not rename/recreate this workflow or
reset its numbering without migrating the version-code policy above all
previously shipped codes. Failed runs can leave harmless gaps.

Select a committed, reviewed source SHA on `main`, with the release workflow
already present. Require green normal CI and appropriate local gameplay
checks for changed features before publishing. Write release notes describing
requirements, changed behavior and testing limits. The repository must be
private, and the tag must resolve to the exact selected commit. Existing tags
must never be moved to different code.

The GitHub release editor can create the release. An equivalent explicit
CLI sequence is below; publishing is the step that triggers the workflow:

```sh
release_repo=psspssr/pokeemerald-3Ds-dualscreen-thor
release_tag=v0.1.0-alpha.2
release_commit=$(git rev-parse HEAD)
release_notes=build/release-notes.md  # Prepare and review this file first.
test -z "$(git status --porcelain)"
test "$(gh repo view "$release_repo" --json visibility --jq .visibility)" = PRIVATE

gh release create "$release_tag" --repo "$release_repo" \
  --draft --prerelease --target "$release_commit" \
  --title "Emerald Thor ${release_tag#v}" --notes-file "$release_notes"
gh release view "$release_tag" --repo "$release_repo" \
  --json tagName,targetCommitish,isDraft,isPrerelease
gh release edit "$release_tag" --repo "$release_repo" \
  --draft=false --prerelease --latest=false
```

Use the owner's GitHub login or the web UI to publish; events created with a
workflow's own `GITHUB_TOKEN` normally do not start another workflow. Do not
manually upload candidates under the automated asset names. The published
release initially has no APK; its assets arrive after the release workflow
succeeds. Draft creation, ordinary pushes and tag pushes alone do not trigger it.

## What automation verifies

1. The event and live release identify the same private repository, release
   ID, tag and target. The tag must identify the workflow's source commit on
   `main`. These checks repeat before individual uploads.
2. Reusable CI runs source/update/patch tests, sanitizer and native gameplay
   tests, the strict ARM build, harness instrumentation, lint and GLES pixel
   checks. Signing secrets are not passed to it.
3. A separate build produces the non-debuggable production APK with only
   `armeabi-v7a`, minimum SDK 28 and target SDK 35. Packaged native libraries,
   embedded data marker, engine ABI and representative assets must match that
   build's native output.
4. Each job downloads the next input by immutable artifact ID and verifies
   the artifact ZIP digest. The signer aligns the APK before signing; the
   publisher independently verifies its signature, package, hashes and
   manifest before uploading, then downloads all three assets to byte-compare.

The public manifest records source/upstream pins, version/code, package and
SDK/ABI requirements, build tools, engine and packaged-file hashes, signer,
APK size/hash and workflow URL. It reports automated validation separately
from maintainer gameplay QA. Harness success does not establish that the
exact shipped ARM APK has run, and no physical Thor coverage is implied.

After publication, download the exact assets and verify their checksums:

```sh
release_check=$(mktemp -d)
gh release download "$release_tag" --repo "$release_repo" --dir "$release_check" \
  --pattern '*.apk' --pattern build-info.json --pattern SHA256SUMS
(cd "$release_check" && sha256sum -c SHA256SUMS)
```

For exact-artifact gameplay acceptance, install that signed APK on an
ARM-compatible emulator/device. Check startup/Continue, changed features,
save/export, a 1240×1080 second display with touch/drag, Home/resume and display
removal/reconnection. Retain the APK hash, device and observed results. Export
saves before replacing a debug-signed installation; same-key release updates
can use `adb install -r`. See [VALIDATION.md](VALIDATION.md#repeat-the-checks)
for repeatable checks and coverage limits.

## Retry without changing published bytes

Use **Re-run failed jobs** after a build/sign/upload failure. If signing
succeeded, a failed publisher retrieves the original signed bundle by its
artifact ID and digest; it does not rebuild or sign again. The manifest does
not bind to `run_attempt`, so a later attempt can reuse the exact bytes.
Signed bundles are retained for 30 days; unsigned inputs for 14 days.

A rerun with a complete, matching release verifies the existing APK and
assets and skips building. A partial upload accepts only byte-identical
existing assets when retried with its original signed bundle. **Re-run all
jobs** deliberately fails preflight on partial assets: use the failed-job
retry instead. A different source, version/code, signer or existing asset
fails without overwriting or deleting anything. If retained inputs have
expired, recover the exact verified bundle from owner-held evidence or
publish a new version; never replace a published APK with different bytes.

Useful references: [GitHub release events](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#release),
[workflow run numbering](https://docs.github.com/en/actions/reference/workflows-and-actions/variables),
[Android versioning](https://developer.android.com/studio/publish/versioning),
[apksigner](https://developer.android.com/tools/apksigner) and
[zipalign](https://developer.android.com/tools/zipalign).
