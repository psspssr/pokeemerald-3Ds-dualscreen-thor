# Upstream 0.5.0 Android integration — paused handoff

Stopped at the user's explicit request on 2026-11-02. This is a **work-in-progress local checkpoint**, not a validated release. Resume in `/root/fitme/pokeemerald-thor`, branch `main`. Do not push or publish based on this checkpoint alone.

## Request and source identity

The user asked to update the Android/AYN Thor port with the reference 3DS project's features and updates. The Android remote was already current at `8deaeed`. The clean upstream updater imported ZallaxDev `main` at `bcae4b70dac61419fc933084084469f6cfbe344b` (0.5.0), replacing `c330c0a1aece35966b2d899013d16893fe70cff5` (0.2.0).

The updater created local commits `7d77f3b` (squashed upstream), `4f19124` (subtree import), and `ab9be72` (origin.lock). `origin/` remains an unmodified upstream subtree. Android adaptations belong in `android/`, `tools/`, and ordered `patches/android/`, never in `origin/` or directly in generated `build/upstream/`.

## Changes in this checkpoint

- Rebased the Android patch series. Removed overlapping upstream fixes for battle stat panels, capture pages, evolution, shop/assets, and NPC spawn ordering. Preserved Android touch guards, naming keyboard, save safeguards/backups, mystery events, storage recovery, and sharp/MSAA/resolution behavior.
- Removed the obsolete Android bottom-menu crop/inverse mapping overlay: upstream now has a redesigned native bottom-screen interface. Retained whole-party routing before the generic Storage fallback.
- Preserved joinable asset-worker shutdown and Android's voxel work-before-frame-pacing hook.
- Added two-program shader containers (`CTRGLS2`) alongside the existing single-program format; translated the new weather shader's named entry point.
- Added Citro2D triangles, lines, and circles; new graphics queue/cache API compatibility; profiler-only CFG/firmware capability behavior; Home-menu hint state.
- Added framebuffer-aware full-buffer copies for the new Pokédex held-top-image backup/restore. Partial framebuffer copies explicitly fail. CPU texture encoding covers the five framebuffer color formats.
- Added upstream soft-reset `.data`/`.bss` ranges to the Android linker script, including upstream's extra space after battle message text, and included upstream's generated `build/linkid.o` in both links.
- Replaced Nintendo UDS transport with `android/native/src/local_unavailable.c`: unavailable adapter, immediate failed session, no simulated peers. The coverage scanner scans this explicit Android replacement. Actual 3DS radio multiplayer is not implemented on Android.
- Upstream in-game EXP sharing suppresses Android's extra bench award; a nondefault upstream shiny policy suppresses Android's additional multiplier. Existing Android settings remain available. More interaction testing is needed.
- Updated many host fixtures for current production functions. Some older large extraction fixtures were narrowed/replaced where upstream architecture changed (notably battle/evolution/capture and bottom menus). Review coverage changes before considering the work finished.
- Last added overlay `103-upstream-compatibility.patch` retains the popup outline bound outside PORT_BRIDGE, preserves Spinda's embedded-struct fallback, and restores the Android filter preference after upstream temporary scene filter changes.

## Verified so far (pay attention to snapshot limits)

Evidence, scratch helpers, screenshots, and logs are **ignored local files** under `build/evidence/upstream-20261102/`. They are intentionally not committed. `host-results.txt` is an initial, obsolete failure inventory; read the individual newer logs instead.

- Complete native ARM/debug APK build succeeded: `build-link.log`, including Gradle `BUILD SUCCESSFUL`.
- **That APK predates overlay 103 and the final fixture edits. Rebuild before delivery.** It is at `android/app/build/outputs/apk/debug/emerald3ds-android-debug.apk`.
- The full patch series through 103 subsequently passed bootstrap: `bootstrap-final.log`.
- 99 tooling tests passed: `tool-tests.log`.
- 21 GPU host tests passed: `gpu-host.log`.
- SDK declaration coverage passed with zero missing declarations. Final native symbol coverage has not yet been rerun.
- Real GLES emulator suite passed **380 pixel assertions**: `gles.log` and `build/gpu-tests/result.log`. It covers scaling/MSAA, textures, shaders, badges, battle filter restore, and the held-Pokédex framebuffer round trip. Immediately afterward a weather-program shader compilation assertion was added to `offscreen.c`; that latest assertion has **not** been rerun.
- System shim and host configuration tests passed initially.
- Individual updated native tests passed: asset offsets, asset worker, audio worker, bottom menus, contest graphics/hearts, evolution sparkle geometry, FPS/blur persistence, item-menu geometry, menu assets/state, mystery events, naming, Pokédex search, QoL/flee, shop, starter, storage init/carry, Summary, unsupported Berry-transfer entry. See each `native-run_*_tests.log`; do not treat this as a final single-snapshot CI run.
- Installed the pre-103 APK on the existing **read-only** API 30 ARM-translation emulator (`emulator-5582`). `adb install -r` rejected a version downgrade (old probe versionCode 3 vs default debug 1); `adb install -r -d` succeeded. Actual game intro rendered (`boot.png`). This is **not** field/battle/new-feature acceptance. `first-boot.png` shows extraction, `title.png` is another interim capture.
- Emulator was stopped with `adb -s emulator-5582 emu kill` at the user's stop request. No physical Thor was tested. No push, release, or site publication occurred.

## Known unfinished work

1. **Finish the remaining native regression updates and rerun the complete suite.** Last known failures:
   - `run_object_spawn_tests.py`: old fixture asserts removed `OBJECT_EVENT_VIEW_*` constants and extracts the former rectangular spawn algorithm. Upstream now uses camera scoring/eviction. Update to test the new actual helpers (protected player/link/frozen/held objects, furthest candidate, same-step removal-before-spawn) and run upstream `field_view_test.c` too. Do not reintroduce old constants to make the test pass.
   - `run_fossil_spinda_tests.py`: extractor missed `FOSSIL_GFX_LENGTH` because upstream puts comments between `#ifdef PORT_BRIDGE` and the size defines. A regex correction was just made, **not rerun**. Overlay 103 also restores Spinda fallback and must be included.
   - `run_weather_factory_tests.py`: upstream split `ApplyColorMapToRange` and added extended palette helpers. A last edit extracts the common range implementation and supplies original 32-palette fixture macros. **Not rerun**. Inspect remaining Factory failures rather than weakening bounds assertions.
   - `run_save_ui_tests.py`: narrowed to actual bounded message expansion and save acceptance logic; removed obsolete renderer fixture. Last failure was now-unused `gText_Yes/gText_No`, which were just removed, **not rerun**. Add/verify current Save rendering through runtime acceptance or an appropriate new layout test; do not claim old glyph-layout coverage.
   - `run_voxel_stream_timing_tests.py`: actual upstream crossing reserve/margin changed. Latest deterministic observed normal budget is 8.867 ms; crossing should be 3.867 ms given the current fixture. Assertion changed from 4–5 ms to 3.8–3.9 ms, **not rerun**. Inspect subsequent assertions, especially skipped 8x frames.
2. Rebuild the exact final source (including 103), run `check_origin.py --fetch`, symbol coverage, and required CI commands. Engine-only release build and app harness/lint have not been validated this turn.
3. Exercise real-game continuation, new native menus/options, randomizer/Nuzlocke, voxel world/camera/weather, battle, soft reset, pause/resume, save and import/export, and Pokédex top-picture restoration. Only intro was observed so far. Use saved local fixtures without overwriting operator files. Existing save fixtures live in `build/evidence/beta-readiness/`, e.g. `release/runtime/normal-beta1-counter16.sav`.
4. Review GPU copy/cache and queue semantics, shader container rejection cases, and add targeted tests for both shader entries and texture encode/decode round trips. GPU copy readback is intentionally correctness-first; consider performance only after correctness. Queue entries resized by upstream are now freed by C3D_Fini/re-init.
5. Review patch semantics against upstream beyond successful application. Rebase used a scratch Git tree and manual resolution; several initially fuzzy hunks were inspected/reworked. In particular, revisit 073's `bottom` versus upstream's new `down`/`dexTop` behavior and 070's terrain-only AA scope, and test soft-reset input against Android's existing L1+R1 pause shortcut.
6. **Documentation/license update is still pending.** Upstream 0.5.0 changed original port code from MIT to CC BY-NC-SA 4.0 (`origin/LICENSE-PORT.md`). Current NOTICE/README and updating/building docs still describe the old MIT upstream/0.2.0 pin. Correct attribution and applicable scope without relabeling unrelated code. Update feature/validation docs with actual results and limitations. No language selection was added to Android bootstrap; upstream translation sources were imported, but the Android build remains English and regional-ROM language staging is not integrated/tested.
7. Check the final diff, remove obsolete dead test helpers only if replacement coverage is adequate, make a completion commit, and report the actual APK and limitations. Do not label this checkpoint complete.

## Commands and environment

Use the existing local toolchain; the system PATH lacks the required ARM GNU tools:

```sh
cd /root/fitme/pokeemerald-thor
source build/tooling/env.sh
python3 tools/bootstrap.py --make --apk -j6
python3 tools/check_origin.py --fetch
python3 tools/check_shim_coverage.py
python3 -m unittest discover -s tools/tests -v
python3 android/shim/test/run_host_tests.py
python3 android/host/test/run_host_tests.py
python3 android/gpu/test/test_gpu.py
# individual android/native/test/run_*tests.py, then CI-required checks
```

`build/tooling/env.sh` sets local JDK/SDK/NDK/Gradle, plus extracted GNU ARM binaries and LD_LIBRARY_PATH. NDK is `build/tooling/android-ndk-r27c`. Do not add another toolchain unnecessarily.

The first new voxel asset generation took a long time; `build/upstream/3ds_port/romfs/voxel/buildings.bin` grew to about 41 MB. Avoid concurrent bootstraps/builds in the same generated tree. All build sessions launched during this work had finished by handoff.

For the same emulator (read-only protects existing AVD backing data):

```sh
source build/tooling/env.sh
/root/android-sdk/emulator/emulator -avd thor-game-test -read-only \
  -no-window -no-audio -no-snapshot -no-boot-anim \
  -gpu swiftshader_indirect -port 5582
# After boot:
adb -s emulator-5582 shell getprop ro.product.cpu.abilist
adb -s emulator-5582 install -r -d android/app/build/outputs/apk/debug/emerald3ds-android-debug.apk
adb -s emulator-5582 shell am start -n com.emerald3ds.android/.GameActivity
bash android/gpu/test/run-emulator.sh emulator-5582
```

Emulator launch required execution outside the filesystem sandbox for KVM. This was approved. It uses the existing API 30 image in `/mnt/storagebox/fitme-thor/android-sdk/system-images/android-30/google_apis/x86_64/` and reports `armeabi-v7a` support. `adb root` was also run on that disposable/read-only emulator shortly before shutdown; it is not a physical device.

The `build/evidence/upstream-20261102/rebase/` Git repo and helper scripts are scratch evidence, **not authoritative source or safe scripts to rerun blindly**. They contain sequential assumptions and partial intermediate operations. The tracked overlays and production source are authoritative.
