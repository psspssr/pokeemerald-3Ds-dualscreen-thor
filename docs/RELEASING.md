# Private Android prereleases

This workflow covers the authorized private prerelease in
`psspssr/pokeemerald-3Ds-dualscreen-thor`, beginning with **0.1.0-alpha.1**,
version code **1**. Repository visibility stays **private**. This preview may
include its matching generated game data; CI continues to upload diagnostics
only. Public distribution is a separate packaging workflow described in
[BUILDING.md](BUILDING.md#engine-only-build-and-data-packs).

| Build choice | Result |
|---|---|
| Bootstrap without `--release`, then Gradle `assembleRelease` | Signed release candidate can include matching game data and start immediately. |
| Bootstrap with `--release` | Engine-only output; requires an Android-build-specific data pack and import/restart testing. |

## Build a reviewed source commit

Use a clean, committed checkout and the [build environment](BUILDING.md#install-the-tools).
Run the commands below from the repository root in the same Bash session.
For each subsequent shipped build, choose a new version/tag and increase
`versionCode`. Keep the package ID `com.emerald3ds.android`.

```sh
set -euo pipefail
test -z "$(git status --porcelain)"
release_repo=psspssr/pokeemerald-3Ds-dualscreen-thor
release_version=0.1.0-alpha.1
release_code=1
release_tag="v$release_version"
release_commit=$(git rev-parse HEAD)
release_dir="$PWD/build/release/$release_tag"
release_native_out="$PWD/build/android-prerelease-out"
release_tools="$ANDROID_HOME/build-tools/35.0.0"
release_apk_name="emerald-thor-$release_version-armeabi-v7a.apk"
release_apk="$release_dir/$release_apk_name"
mkdir -p "$release_dir"

python3 tools/check_origin.py --fetch
python3 tools/bootstrap.py --make --out "$release_native_out" -j4
android/app/gradlew -p android/app assembleRelease lintRelease \
  "-Pemerald.nativeOut=$release_native_out" \
  "-Pemerald.versionName=$release_version" "-Pemerald.versionCode=$release_code" \
  --no-daemon --max-workers=2
```

Require green CI for the selected code commit and retain its run URL. The
local source/host checks are:

```sh
python3 -m unittest discover -s tools/tests -v
python3 android/shim/test/run_host_tests.py
python3 android/host/test/run_host_tests.py
python3 android/gpu/test/test_gpu.py
python3 android/native/test/run_qol_tests.py
python3 tools/check_shim_coverage.py
```

Also run the current harness instrumentation, lint and GLES device checks
from [BUILDING.md](BUILDING.md#emulator-app-and-thor-window-tests) and
[VALIDATION.md](VALIDATION.md#repeat-the-checks).
Use the current tests and results rather than copying an earlier release's totals.

## Align, sign and inspect

The persistent owner-managed signing identity is
`/root/.local/share/emerald-thor/signing/release.keystore`, alias `emerald-thor`;
its password file is `release.pass` in the same directory. Retain an
owner-controlled backup and owner-only permissions. Never print or commit
either secret file, or replace this key for an ordinary update. Android
updates require the same signing identity.

```sh
"$release_tools/zipalign" -P 16 -f 4 \
  android/app/build/outputs/apk/release/emerald3ds-android-release-unsigned.apk \
  "$release_dir/aligned-unsigned.apk"
"$release_tools/apksigner" sign \
  --ks /root/.local/share/emerald-thor/signing/release.keystore \
  --ks-key-alias emerald-thor \
  --ks-pass file:/root/.local/share/emerald-thor/signing/release.pass \
  --debuggable-apk-permitted false --v4-signing-enabled false \
  --out "$release_apk" "$release_dir/aligned-unsigned.apk"
"$release_tools/apksigner" verify --verbose --print-certs "$release_apk" \
  > "$release_dir/signature-check.txt"
"$release_tools/zipalign" -c -P 16 -v 4 "$release_apk" \
  > "$release_dir/alignment-check.txt"
"$release_tools/aapt" dump badging "$release_apk" > "$release_dir/package-check.txt"
```

The PKCS12 key uses the keystore password; omit a separate `--key-pass`.
Compare the public certificate SHA-256 with the previous release. The initial
certificate is `eeb95f89fcb944d3a62cc2aa8d0bb720584333d476c13b5a823ef486fdcd0389`.
Align before signing; changes to the signed APK invalidate its signature.
See the official [apksigner](https://developer.android.com/tools/apksigner)
and [zipalign](https://developer.android.com/tools/zipalign) references.

Check the final APK's package/version, minimum SDK 28, target SDK 35,
**non-debuggable** status and sole ABI `armeabi-v7a`. Byte-compare both packaged
native libraries with `$release_native_out/jniLibs/armeabi-v7a/`, and match the
embedded-data marker, engine ABI, bottom-menu asset and voxel shader against
that same native output.

## Test and freeze the signed artifact

Install **the signed APK above** on an ARM-compatible emulator/device. Export
saves before replacing a debug-signed installation; its certificate differs.
Same-key release updates can use `adb install -r`. Check fresh startup,
import/Continue, battle and level-up UI, save/export, phone layouts, a
1240×1080 second display with touch/drag, Home/resume, removal/reconnection,
and the default-off QoL settings. Exercise changed features and record the
device, APK hash and remaining coverage limits. Harness success alone does
not establish that this artifact ran.

Create `build-info.json` in `$release_dir` with the exact source SHA, origin
and pret pins, version/code, APK filename/size/SHA-256, signer SHA-256,
SDK/ABI/tool versions, engine ABI, packaged-library hashes, passing CI URL,
and a summary of this signed artifact's runtime checks. Regenerate it after
any rebuild. Record the same identity in the release notes and validation
report; do not reuse metadata from an earlier candidate.

```sh
(cd "$release_dir" && sha256sum "$release_apk_name" build-info.json > SHA256SUMS)
```

Keep these exact signed bytes after QA. A later code change requires a new
build, signature, checksums and appropriate artifact validation.

## Draft, verify the uploaded bytes, then publish

Recheck that the explicitly named repository is private. Use a new tag; if a
tag/draft already exists, inspect its target before proceeding and never move
an existing published tag to different code. Write reviewed notes to
`$release_dir/notes.md`, including requirements and hardware/testing limits.
The full SHA below prevents a changing default branch from selecting the
release's source. [GitHub CLI release creation](https://cli.github.com/manual/gh_release_create)
documents tag creation and `--target` behavior.

```sh
test "$(gh repo view "$release_repo" --json visibility --jq .visibility)" = PRIVATE
gh release create "$release_tag" --repo "$release_repo" \
  --draft --prerelease --target "$release_commit" \
  --title "Emerald Thor $release_version" --notes-file "$release_dir/notes.md"
gh release upload "$release_tag" --repo "$release_repo" \
  "$release_apk" "$release_dir/SHA256SUMS" "$release_dir/build-info.json"

release_verify=$(mktemp -d "$release_dir/download-check.XXXXXX")
gh release download "$release_tag" --repo "$release_repo" --dir "$release_verify" \
  --pattern "$release_apk_name" --pattern SHA256SUMS --pattern build-info.json
(cd "$release_verify" && sha256sum -c SHA256SUMS)
cmp "$release_apk" "$release_verify/$release_apk_name"
cmp "$release_dir/build-info.json" "$release_verify/build-info.json"
cmp "$release_dir/SHA256SUMS" "$release_verify/SHA256SUMS"
gh release view "$release_tag" --repo "$release_repo" \
  --json tagName,targetCommitish,isDraft,isPrerelease,assets
```

After the target, assets, notes and byte comparisons are correct:

```sh
gh release edit "$release_tag" --repo "$release_repo" --draft=false --prerelease --latest=false
test "$(gh api "repos/$release_repo/commits/$release_tag" --jq .sha)" = "$release_commit"
gh release view "$release_tag" --repo "$release_repo" --json url,isDraft,isPrerelease
```

Attach only the tested APK, checksums and public build manifest. Signing
material, raw saves, ROMs, debug/unsigned APKs and the local evidence directory
remain outside the release. Publishing does not change repository visibility.
