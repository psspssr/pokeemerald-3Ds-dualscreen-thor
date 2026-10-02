# Validation — 2026-10-02

The complete ARM game builds, runs on an Android emulator, and exchanges real
saves with the original GBA game. Outdoor voxel rendering, the secondary
touch window, Home/resume and display removal were exercised. **Final CI,
broader gameplay and physical Thor validation remain open.**

Code checkpoint: `d9f48e11772d1cee66b4ae91b395f7204ce8062a`, including the
secondary-touch, streamed-material, frame-pacing and clean-emulator fixes.
Earlier development SHAs changed during the author-identity correction.
Session commits now use the verified `psspssr` GitHub account identity;
the original imported history was preserved.
Use the pinned sources and APK hash below to identify this test build.

## Sources and build

| Item | Verified value |
|---|---|
| Imported 3DS project | `ZallaxDev/pokeemerald-3Ds-dualscreen` at `4c64da2644223668c60e59c7731347baada7295d` |
| Unchanged `origin/` Git tree | `6d909815a5ffaede2e8f31ea0df5d08a6acee73a`, checked against the actual upstream commit |
| pret/pokeemerald pin | `76463dac15cad36aca5e2b3f6366abf9e53f814f` |
| Production ABI | `armeabi-v7a` only; strict native link and fixed-address loader checks passed |
| Development APK | `android/app/build/outputs/apk/debug/emerald3ds-android-debug.apk`, **34,722,799 bytes** |
| APK SHA-256 | `cb8a7550852ff468884996b09237ee78d6a9c4680bbcd744b45a8c40ce83bce5` |

Both packaged native libraries were byte-compared with the native build
outputs. Packaging checks also matched the embedded-data marker, engine ABI,
bottom-screen menu asset and translated voxel shader. The engine-only native
package also built successfully. The development APK and generated game data
remain local test artifacts.

## Completed checks

| Check | Result and boundary | Local evidence under `build/evidence/` |
|---|---|---|
| App build and lint | Debug and separate harness APKs built; lint passed. | `app-unbuffered-build.log` |
| App instrumentation | **19 tests passed** on API 34: the original 15 passed in both 1080×1920 portrait and 1920×1080 landscape; three mGBA tests and the secondary-drag regression passed in expanded suites. Fresh-data runs with the corrected fullscreen fixture and the final AOSP image also passed all 19. | `app-review-{portrait,landscape}.log`, `app-review-mgba-instrumentation.log`, `app-unbuffered-instrumentation.log`, `app-ci-fixture-instrumentation.log`, `ci-aosp-instrumentation.log` |
| Thor-sized app window | A 1240×1080 presentation display rendered independently; visible touch response, coordinates, reversed ordering, display loss/reconnect, resize, recreation and pause were checked. This row uses the harness renderer. | `test-evidence/thor-*.png`, app instrumentation logs |
| System shim | Production filesystem, allocation, input, thread/lock, PCM and lifecycle tests passed with ASan and UBSan. | `shim-tests.log` |
| Host graphics | **15 tests passed** for texture layout/conversion, shader translation and frame pacing, including rejected unsupported inputs and overdue/resumed frame deadlines. | `gpu-host-final.log` |
| Android GLES backend | **46 pixel assertions passed**: the original 39 cover 2D rendering, texture edits, TexEnv, blending/depth, rotated screens, framebuffer sampling, voxel shaders, batching and mixed CPU/GPU output; seven further checks cover arena texture views and safe reuse. | `gpu-sprite-batch-after.log`, `gpu-arena-texture-test.log` |
| Tooling | **30 tests passed**, including updater safety, save validation, discarded startup code, truly absent definitions and missing link inputs. | `tool-tests-final.log` |
| Real game and saves | The ARM APK ran on an API 30 emulator with ARM translation: intro, character creation, starting area, save/load, GBA interchange, textured outdoor voxel rendering, dual-screen Save/Options, Home/resume and unplug fallback. | Game screenshots, save reports, `game-final-dual-resumed.png`, `game-final-display-fallback.png` |
| Audio | The real game opened a **44,100 Hz AAudio stream**. A production NDSP probe produced 701 PCM frames with 1,401 nonzero samples. The emulator host used `-no-audio`, so output was **not auditioned**. | `vanilla-save-in-android.log`, `native-audio-probe.log` |
| Launcher | The new adaptive launcher artwork was built and visually checked in the Android launcher. | `launcher-icon.png`; [icon source and prompt](ICON.md) |

Logs, screenshots, ROMs, saves and APKs in `build/evidence/` are local and are
**not committed**. This report preserves the outcomes and artifact identities;
the commands below reproduce the automated checks.

## Performance and frame pacing

The real-game emulator uses ARM translation and SwiftShader. The host has an
Intel graphics device but no bound driver or `/dev/dri` render node; hardware
graphics was not enabled as part of this task. These are software-emulator
measurements, not Thor hardware benchmarks.

Single-display 2D and warm voxel scenes reached approximately **59.3–59.9 FPS**.
Dual voxel samples were **39.8–43.4 FPS**. A slower dual-2D interval reached
**20.5–27.3 FPS** and recovered after surface recreation; it did not accompany
an input, save or lifecycle failure. The later controlled traces were:

| Twelve-second trace | Frames/second | Combined EGL swaps per frame | Outside-swap sleeps of at least 15 ms |
|---|---:|---:|---:|
| Before pacing repair, recreated surfaces | 52.95 | 9.97 ms | 20 |
| Before pacing repair, following a mode change | 42.62 | 12.28 ms | 49 |
| After pacing repair | 48.32 | 11.52 ms | **0** |

The scheduler used to add another full interval when resynchronizing after an
overdue frame. It now skips that unnecessary wait and anchors a resumed frame
before rendering. Deterministic tests preserve the 59.83 Hz cadence for frames
within budget and reject extra sleeps after overruns. The real trace confirms
the long sleeps disappeared. Software-renderer and resource state varied, so
the FPS difference alone is not an isolated measurement of this repair.

Game logic remained around 0.1 ms; much of the remaining time was paid in EGL
rendering/flush/presentation. Separate graphics benchmarks reduced a one-glyph
atlas update from 6.259 to 0.198 ms and 17 transformed sprites from 1.497 to
0.189 ms, with pixel assertions retained. Sustained full-speed dual rendering
still requires a physical-device benchmark. Evidence: `performance-followup.json`,
`dual-2d-*-summary.json`, `gpu-pacing-{host,device}.log`, and the atlas/sprite
benchmark logs.

## Real GBA save interchange

A pristine reference Emerald ROM was built from source, with SHA-1
`f3ae088181bf583e55daf962a92bb46f4f1d07b7`, and run in **mGBA 0.10.2**.
Two independent paths were exercised:

1. Android created player **A**, save counter **1**. The original GBA game
   loaded it and saved counter **2**. Android's real Settings/system-document
   picker then imported that GBA save, applied it on restart, and exported it
   through the document picker. The provider read, pending import, applied
   file and exported file were all identical **131,072-byte** raw saves.
2. The original GBA game created player **AAAAAAA**. Android loaded that save
   and saved it again with counter **2**. Slot signatures, section checksums,
   player identity and counters were checked independently.

The first path's GBA result and Android import/export all have SHA-256:

```text
9e64b0d22a882c267641e4c78e577dcdf7f595616b1b01912cb6a067d197cba4
```

Evidence: `android-first-save.json`, `android-gba-roundtrip.json`,
`gba-reference-save.json`, `vanilla-resaved-by-android.json`,
`saf-{provider-read,import-pending,import-applied}.sav`, and
`roundtrip-app-export.sav`. mGBA's optional 16-byte RTC trailer is normalized
by the importer; raw exports contain no Android header or stale RTC trailer.
Port options stay separate, and emulator clock overrides are not transferred.
See [save formats and transfer instructions](BUILDING.md#engine-only-build-and-data-packs).

## Findings resolved and work remaining

The real API 30 ARM run exposed a secondary-display **MOVE-event ANR** that
the API 34 harness had not reproduced. The UI thread was idle while native
rendering continued. The game touch view now requests unbuffered dispatch
at the start of each gesture, including while gameplay input is disabled.
The original system-generated DOWN/MOVE/UP sequence then opened Save, remained
responsive after a seven-second wait, and accepted Cancel and Options.
No new ANR trace appeared. The added system-input regression also passed
while rendering and paused. Evidence: `game-dual-anr.txt`,
`game-dual-fixed-save-touch.png`, `game-dual-fixed-options.png`, and
`app-unbuffered-instrumentation.log`.

The system picker initially ignored API 30 `adb input tap` events on document
rows. Explicit finger events worked and completed the real import/export
above; this was an automation input issue. The reproducible helper is in
[BUILDING.md](BUILDING.md#emulator-app-and-thor-window-tests).

The outdoor voxel check found white building materials from upstream texture
views initialized in its streaming arena. Lazy GPU records and ownership-safe
reuse repaired that path; seven new pixel checks passed and textured buildings
were visually confirmed in the real game (`game-voxel-textured.png`,
`game-dual-voxel-textured.png`). Home/resume restored both scenes; removing the
secondary display restored the combined layout and on-screen controls without
a new ANR (`game-paused.log`, `game-resumed.log`, final screenshots above).

CI also exposed a fixture issue: the system's first-run fullscreen tutorial
intercepted injected control taps. Its focus window and interception were
captured, then reproduced locally. Setting
`immersive_mode_confirmations=confirmed` removed the tutorial; the full
19-test suite then passed from cleared harness storage. CI now prebuilds and
lints before starting the emulator, with bounded Gradle memory/workers.
Input and pixel assertions were retained. Evidence: `ci-immersive-interception.txt`,
`ci-onboarding-{before,after}.xml` and the fixture instrumentation log.

The CI fixture uses the standard API 34 AOSP image with emulator **36.6.11**,
verified by archive SHA-256, at 720×1280 and 320 dpi. Its secondary presentation
display remains 1240×1080. This avoids unrelated Google services startup ANRs
observed on the hosted runner while keeping GLES rendering enabled. The same
fresh AOSP fixture passed all **19 app tests and 46 GLES assertions** locally;
see `ci-aosp-instrumentation.log`, `ci-aosp-gpu.log` and
`ci-aosp-after-tests-system.log`.

Remaining acceptance work at this checkpoint:

- **Not yet covered:** broader battles/menus, long gameplay sessions, full-game
  progression, audible audio quality, and sustained performance/thermal tests.
- **Hardware pending:** no physical AYN Thor was tested. Lid behavior, actual
  panel timing and model/firmware ABI support still require device checks.
- **CI pending:** [run 37009341606](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/actions/runs/37009341606)
  tests the final code and emulator fixture. Earlier runs exposed hosted-runner
  dependencies and fixture issues described above; the final run must finish
  before CI can be called green.

The renderer targets the current pin's **straight-line PICA shaders and
uncompressed textures**; it does not implement the entire 3DS graphics API.
Unsupported shader instructions/declarations fail the build; compressed
texture formats are rejected. A new upstream pin still needs compile, pixel
and gameplay checks. Neither these targeted checks nor the save interchange
establishes that the whole game is fully tested.

## Repeat the checks and validate an update

After setting up the SDK/NDK environment from [BUILDING.md](BUILDING.md):

```sh
python3 tools/check_origin.py --fetch
python3 -m unittest discover -s tools/tests -v
python3 android/shim/test/run_host_tests.py
python3 android/gpu/test/test_gpu.py
python3 tools/bootstrap.py --make --apk -j4
python3 tools/check_shim_coverage.py
bash android/gpu/test/run-emulator.sh emulator-5580
android/app/gradlew -p android/app connectedHarnessAndroidTest lintHarness
```

Use the appropriate connected-device serial for the graphics check. To inspect
a locally exported save without modifying it:

```sh
python3 tools/verify_gba_save.py build/evidence/roundtrip-app-export.sav
```

For the next upstream version, preview and import with
[`sync_origin.py`](UPDATING_FROM_ORIGIN.md), rebuild, regenerate any matching
data pack, and repeat the [runtime acceptance checklist](BUILDING.md#runtime-acceptance-after-an-upstream-update).
Record the new pins, APK hash, device ABI, automated results and remaining
limitations in this report.
