# Validation

## Latest preview: 0.1.0-alpha.8

The [signed alpha.8 release](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/releases/tag/v0.1.0-alpha.8)
passed all seven [release workflow jobs](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/actions/runs/37239789444).
The exact downloaded APK was then installed and tested in an ARM-compatible
Android emulator. **Physical Thor validation remains open.**

| Release identity | Value |
|---|---|
| Source | `3019b32476e72c77d2b41210a71312584e409a89` |
| Version / Android code | `0.1.0-alpha.8` / **8** |
| APK | `emerald-thor-0.1.0-alpha.8-armeabi-v7a.apk` |
| Size | 27,747,524 bytes |
| SHA-256 | `9545aff511bafe6a7ff8afea68cc2555734012d4bbe9f9b1f5cb364748afa5be` |
| Engine/data ABI | `399ac6d4` |

The downloaded signature, alignment, package requirements and packaged-file
hashes match `build-info.json`. The signing identity is unchanged. Installed
APK bytes match the published checksum; installation preserved the save and
both settings files.

## Automated checks

The release source also passed [normal CI](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/actions/runs/37238231335).

| Coverage | Result |
|---|---|
| Android app, input, storage and display lifecycle | 119 app tests; harness and release lint passed |
| Build, updater, saves, overlays and website tooling | 102 tests passed |
| OpenGL ES rendering | 270 pixel assertions passed |
| 3DS SDK coverage | 301 identifiers; no missing declarations or linked functions |
| Native code | Host, gameplay and sanitizer checks passed |

Regressions cover starter alignment, Sharp defaults and explicit Smooth
choices, expanded menu touch mapping, voxel MSAA depth/edge handling and
resource reuse, FPS defaults and persistence, and 2×/4×/8× fast-forward.
The AA selector tests require visible options and real finger taps.

The bug sweep also repaired whole-screen battle Party classification and an
unbounded Save-message expansion. ASan reproduces the old overflow; bounded
expansion, malformed placeholders and complete confirmation layout are tested.
The imported upstream tree remains unchanged from its recorded pin.

## Emulator playtests

The game runs as ARMv7 through ARM translation and SwiftShader. Tests use a
1920×1080 main display and a separate 1240×1080 touch display. Screenshots are
inspected in addition to checking process health.

- **Exact published APK:** Continue loaded the expected normal save. Sharp
  filtering, expanded Bag, a single Close Bag tap returning to Map, visible
  Off/2×/4× anti-aliasing choices and return from Settings passed. The bounded
  log window contained no fatal, ANR or GPU-failure matches.
- **Matching release candidates:** all three starter previews were centered;
  confirmation and cancellation worked. Battle Party selection and SHIFT,
  full Save warnings and both No/Yes paths passed. FPS Off/On/Off, actual
  2×/4× MSAA allocations, L2 hold and R2 toggle at 8×, Fit/Fill and display
  removal/reconnection were exercised.
- **Save preservation:** QA saves and settings were backed up and restored
  byte-for-byte. A normal overwrite produced a validated Emerald save;
  declining it preserved the previous bytes.

The final candidate's engine differs from the published engine only in its
GNU build-ID; executable code and data are identical. Earlier geometry
captures are identified separately in the local QA records. These checks are
bounded playtests, not a natural full-game completion.

Earlier previews were tested through early-game progression, ordinary and
voxel battles, PC deposit/withdrawal, save backup rotation, shared experience,
and targeted mystery-event journeys. Those checks were not all repeated on
alpha.8. Android saves also completed a round trip through original GBA Emerald
in mGBA; raw save and party data remained compatible. Port preferences stay
outside the `.sav` file. See [save transfer details](BUILDING.md#engine-only-build-and-data-packs).

## Known limits

- Physical Thor lid sensing, firmware compatibility, panel timing, sustained
  performance, memory use, thermals and battery life need hardware testing.
- Audio-stream and PCM checks passed, but audible quality was not assessed.
- Selecting 8× requests that speed; it does not guarantee 8× throughput.
  Emulator frame rates are not hardware benchmarks.
- Long emulator sessions show driver-side memory growth reproduced by a
  separate EGL-only program. This does not establish Thor memory behavior.
- Some upstream affine, OBJ-window and BG-mosaic limitations remain. The
  renderer does not implement every 3DS graphics feature, and later-game
  scenes still need broader testing.

Use the [Thor hardware checklist](testing/HARDWARE_TEST.md) for a repeatable
real-device pass. Diagnostic export is opt-in and uploads nothing automatically.
Saves, raw logs, ROMs and local QA fixtures are not checked into the repository.
The [screenshot gallery](images/README.md) distinguishes gameplay captures
from the illustrative device mockup.

## Repeat the checks

After [installing the build tools](BUILDING.md#install-the-tools):

```sh
python3 tools/check_origin.py --fetch
python3 -m unittest discover -s tools/tests -v
python3 android/shim/test/run_host_tests.py
python3 android/host/test/run_host_tests.py
python3 android/gpu/test/test_gpu.py
python3 tools/bootstrap.py --make --apk -j4
python3 tools/check_shim_coverage.py
python3 android/native/test/run_qol_tests.py
python3 android/native/test/run_mystery_tests.py
python3 android/native/test/run_summary_tests.py
python3 android/native/test/run_evolution_tests.py
python3 android/native/test/run_starter_tests.py
python3 android/native/test/run_fps_settings_tests.py
python3 android/native/test/run_bottom_menu_tests.py
python3 android/native/test/run_save_ui_tests.py
```

Select the intended connected emulator before running device tests:

```sh
adb devices
export ANDROID_SERIAL=emulator-5554  # Replace with your device's serial.
bash android/gpu/test/run-emulator.sh "$ANDROID_SERIAL"
android/app/gradlew -p android/app connectedHarnessAndroidTest lintHarness
```

Use the [upstream-update acceptance checklist](BUILDING.md#runtime-acceptance-after-an-upstream-update)
after an import and the [release guide](RELEASING.md) when publishing a new APK.
