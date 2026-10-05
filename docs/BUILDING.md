# Building and testing the Android port

The supported build host is Linux x86_64. The game APK contains **32-bit ARM
only**; check the device's actual ABI list before installing. The separate
display-test APK also contains x86_64 so the Android UI can be tested on a
standard hardware-accelerated emulator.

This guide builds the checked-out source, which pins upstream `c330c0a1aece`
(0.2.0). The published alpha.8 APK retains its earlier `cdc77a3d2b01` pin.
Updating source does not change an existing release.

## Install the tools

Use Python 3.11+, JDK 17, Git (including `git subtree`), GNU Make, a host C/C++
compiler, Pillow, libpng headers, GNU ARM binutils and the GNU ARM C preprocessor.
On Ubuntu 24.04:

```sh
sudo apt-get install build-essential git python3 python3-pil libpng-dev \
  binutils-arm-linux-gnueabi gcc-arm-linux-gnueabi openjdk-17-jdk
```

Install the Android SDK command-line tools, then set `ANDROID_HOME` to their
SDK directory. `ANDROID_SDK_ROOT`, if set, should refer to the same directory.

```sh
export ANDROID_HOME="$HOME/Android/Sdk"
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"
sdkmanager --licenses
sdkmanager 'platforms;android-35' 'build-tools;35.0.0' 'ndk;27.2.12479018'
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/27.2.12479018"
```

The project pins NDK r27c, Gradle 8.10.2, Android Gradle Plugin 8.7.3 and
Kotlin 2.0.21. Gradle uses its checked-in wrapper. A separately installed NDK
can be selected with `--ndk /absolute/path` or `ANDROID_NDK_HOME`.

## Build the game

Run from the repository root:

```sh
python3 tools/check_origin.py --fetch
python3 tools/bootstrap.py --make --apk -j4
```

The bootstrap fetches the pret commit in `origin/upstream.lock`, applies
upstream's patches and the strict `patches/android/` overlays, generates
assets, compiles the complete game and Android platform layer, validates the
native ELF, and packages the debug APK. The first build downloads sources and
Gradle dependencies and needs several GB of scratch space. Repeating the command reuses build outputs.

| Output | Location |
|---|---|
| Development APK, embedded game data | `android/app/build/outputs/apk/debug/emerald3ds-android-debug.apk` |
| Native libraries and packaged assets | `build/android-out/` |
| Generated source/build tree | `build/upstream/` |
| Unstripped native executable and link maps | `build/upstream/3ds_port/emerald3ds.elf`, `build/upstream/3ds_port/build/*.map` |

Keep source changes under this repository's `android/`, `tools/` and
`patches/android/`; `origin/` must stay identical to its pin, and
`build/upstream/` is generated. Use a separate `--dir` if you need an independent
build. `--clean` rebuilds that generated tree and discards its local edits.

Development APKs embed generated game data for local testing. The
authorized private prereleases package that full native/data output in signed,
non-debuggable Gradle release APKs; follow [RELEASING.md](RELEASING.md).
The repository remains private. Normal push/PR validation uploads diagnostics.
The separate release workflow uploads a signed APK only after a GitHub release
is published; see [automatic releases](RELEASING.md).

## Engine-only build and data packs

```sh
python3 tools/bootstrap.py --make --apk --release --out build/android-release-out -j4
```

The bootstrap's `--release` flag packages engine assets without the game data.
It is distinct from Gradle's `assembleRelease`, which selects the Android
release variant and packages whichever native output it is given. The
engine-only result is
`android/app/build/outputs/apk/release/emerald3ds-android-release-unsigned.apk`;
signing and verification are covered in [RELEASING.md](RELEASING.md).

An **engine-only** APK needs `emerald3ds.pak` generated for the **same Android build**.
The data ABI includes executable-specific pointers. A stock 3DS pack, or a
pack from an older Android build, is not interchangeable. The game checks the
pack's ABI when loading it. Import the pack from the app's Settings, then
restart the game to apply it.

For local validation, make a matching pack from this build's generated data:

```sh
python3 build/upstream/tools/port_common/staging.py pak \
  --romfs build/upstream/3ds_port/romfs \
  --out build/emerald3ds.pak
```

This locally generated pack also contains game data; keep it private. A public
release with a player-ROM builder needs an Android-build-specific recipe and
the upstream release provenance checks. The upstream builder documentation
describes that workflow in [origin/builder/README.md](../origin/builder/README.md).
An engine-only APK by itself does not provide that recipe.

Saves use Emerald's original raw 128 KiB flash format, without an Android
header. **Settings → Saves & data → Import save** accepts a GBA/emulator/3DS
`.sav` of 128 KiB or a 64 KiB first-slot recovery image; the game fills a missing
second half with erased flash bytes in memory. A recovery file remains 64 KiB on disk
until an in-game save writes the full flash image, and an earlier export
retains that size. Emulator save states are unsupported; export a raw
battery/flash save from the emulator first.

Development builds also verify Emerald's section IDs, checksums, save counters
and slot layout before staging an import and again before replacing the live
save. An intact older slot is accepted when a newer write was interrupted.
Invalid imports preserve the current save and its backup. If an older app left
an invalid pending import, the next launch sets it aside and reports the
problem; choose **Retry** to continue with the existing save. This validates
the flash format without changing Pokémon data.

**Export save** writes the raw file; choose the filename your emulator expects
(commonly the ROM basename plus `.sav`). Export pauses the native game through
completion so an in-progress save write cannot be copied halfway through.
Export also keeps a private recovery copy until completion. If the chosen
destination points back to the live save and the provider fails while writing,
the original is restored. If storage prevents restoration, the app keeps the
copy and pauses gameplay; retry the export or restart after storage is available.

The port keeps its voxel/camera options, including `3D BATTLE`, separately at
`sdmc/3ds/emerald3ds/settings.txt`; Android screen and control preferences are
app settings. Neither is inserted into the GBA `.sav`. Transfer the raw save
alone to a GBA emulator, and keep the settings file separately if retaining
port options. Importing a replacement keeps the previous raw save as
`emerald3ds.sav.bak`. Imported files apply at the next game launch.

mGBA 0.10.2 may append a 16-byte RTC record, producing a 131,088-byte file.
The importer checks that exact size, BCD/calendar fields, RTC control bits and
Unix timestamp before removing the trailer. It also recognizes mGBA's initial
zero-time/control-0x40 record. This format has no identifying signature, so
these are structural checks rather than proof of which program wrote it.
The format is defined by mGBA's
[RTC savedata code](https://github.com/mgba-emu/mgba/blob/0.10.2/src/gba/savedata.c#L591)
and [RTC initialization](https://github.com/mgba-emu/mgba/blob/0.10.2/src/gba/cart/gpio.c#L85).

The original import file is untouched. A best-effort copy of the last imported
RTC trailer is retained as `last-imported-mgba-rtc.bin` for reference; it is
never loaded or appended to later exports. Android uses the device clock, so
an emulator's clock override is **not transferred**. Exports remain canonical
raw flash saves for GBA hardware and other emulators.

## Install and run the real game

With USB debugging enabled and the device connected:

```sh
adb shell getprop ro.product.cpu.abilist
adb install -r android/app/build/outputs/apk/debug/emerald3ds-android-debug.apk
adb shell am start -n com.emerald3ds.android/.GameActivity
```

The ABI list must contain `armeabi-v7a`. Android version and CPU model alone
do not establish that the OS can run this build. A 64-bit-only emulator will
reject the APK even if it can run the display-test APK.

On Thor, enable its second panel in the device settings and keep the app's
dual-display setting enabled. The app creates a separate bottom-screen
window; display ordering can be reversed in Settings. Removing the secondary
display restores a single-display layout. See [AYN_THOR.md](AYN_THOR.md).

## Automated checks

```sh
python3 -m unittest discover -s tools/tests -v
python3 android/shim/test/run_host_tests.py
python3 tools/check_shim_coverage.py --headers-only
```

The updater tests use temporary local Git repositories. The shim tests compile
production C code with AddressSanitizer and UndefinedBehaviorSanitizer; they
exercise filesystem/save round-trips, allocation metadata, input transitions,
threads and locks, PCM output, and suspend/resume. The header checker is an
early warning for upstream SDK changes. After the game build, also run:

```sh
python3 tools/check_shim_coverage.py
python3 android/native/test/run_summary_tests.py
python3 android/gpu/test/test_gpu.py
```

Summary tests compile the generated, patched handlers under ASan/UBSan. They
cover asset decoding, learning/HM refusal, tap preview and confirmation,
reordering, and the visible Cancel row. GPU host tests cover shader, texture,
pacing and queue logic; real GLES checks require a connected emulator/device:

```sh
bash android/gpu/test/run-emulator.sh emulator-5554
```

Replace `emulator-5554` with the intended serial from `adb devices`. These
pixel checks also exercise upstream's wrapped `GX_BindQueue` callback
and per-frame upload accounting. Keep the link wrappers recorded in
`build/upstream/3ds_port/build/android.wrap`; bypassing the callback can stop
voxel uploads even when the native link succeeds.

The native coverage check needs an ARM-capable `nm` and the NDK (`--nm` and
`--ndk` may select them explicitly). Native linking separately checks final
symbol resolution, relocations and the fixed-address game-data layout.
Host/symbol checks alone do not establish graphics correctness; emulator
pixel checks do not establish real-device frame rate.

## Emulator app and Thor-window tests

Use Android Studio's Device Manager or the command line to start a Google
APIs x86_64 emulator with GLES 3 support. For a fresh API 35 test device:

```sh
sdkmanager emulator 'system-images;android-35;google_apis;x86_64'
echo no | avdmanager create avd --name thor-app-test \
  --package 'system-images;android-35;google_apis;x86_64' --device pixel_2
"$ANDROID_HOME/emulator/emulator" -avd thor-app-test -no-snapshot
```

In a second terminal, with the same SDK/NDK environment:

```sh
cd android/app
./gradlew connectedHarnessAndroidTest lintHarness
```

The `harness` variant has a separate package
`com.emerald3ds.android.harness` and the launcher name **Emerald display test**.
It builds a small native renderer to exercise the JNI host, controls, display
surface lifecycle and storage flows independently of game data. The
instrumentation suite creates a 1240×1080 virtual presentation display,
checks the second window and touch routing, and checks fallback after removal.
The tests also cover multitouch controls and invalid save/data-pack imports.
Reports are in `android/app/build/reports/`.

For manual inspection of Android's secondary-display behavior, enable an
overlay display while the emulator is running:

```sh
adb shell settings put global overlay_display_devices '1240x1080/240'
```

Launch **Emerald display test**, inspect both surfaces, tap the bottom picture,
reverse display ordering in Settings, background/resume the app, then remove
the overlay:

```sh
adb shell settings delete global overlay_display_devices
```

The harness validates the app and host interface. The complete game still
needs an ARM-compatible emulator or physical device. Android documents
headless operation and GPU options in its
[emulator command-line guide](https://developer.android.com/studio/run/emulator-commandline).

On an API 30 emulator, `adb shell input tap` may move around the system file
picker but fail to activate a document: that command injects an unknown tool
type, while the document selection handler expects a finger or mouse. The QA
helper below injects an explicit finger event. It is never part of the APK.

```sh
mkdir -p build/android-input
javac -source 8 -target 8 -classpath "$ANDROID_HOME/platforms/android-35/android.jar" \
  -d build/android-input docs/testing/AndroidTouch.java
"$ANDROID_HOME/build-tools/35.0.0/d8" --min-api 28 --output build/android-input \
  build/android-input/AndroidTouch.class
adb push build/android-input/classes.dex /data/local/tmp/emerald-touch.dex
adb shell CLASSPATH=/data/local/tmp/emerald-touch.dex app_process / AndroidTouch 400 500
```

The optional third argument selects a display ID. Use coordinates from
`adb shell uiautomator dump`, rather than a resized screenshot. This allows
the production Settings import/export flow to be checked through the actual
system picker and `content://` providers, in addition to file-copy tests.

## Runtime acceptance after an upstream update

Record the Android commit, upstream pin, APK hash, device/emulator ABI list,
Android version, and renderer. With the real game APK, exercise the intro,
title screen, entering the overworld, audio, save/reload and bottom-screen
menus. On current dev, compare classic battles with **VOXEL 3D + 3D BATTLE**
enabled: check move animations, both level-up stat pages and returning to the
field. The Pokémon and battle interface remain 2D over the voxel scenery.

In Party/Summary, test row preview, second-tap confirmation, move reordering
and the visible Cancel row; also test learning a fifth move, refusal to forget
an HM, and canceling. On two displays, check bottom touch coordinates, gamepad
mapping, background/resume, display removal/reconnection, and lid close/open
on actual Thor hardware.

Compare visible rendering with the pinned 3DS version. Measure sustained frame
times and audio behavior during gameplay; a host harness frame count or an
emulator's speed cannot establish Thor performance. Keep real-game results
separate from harness results when reporting validation.

The [Android workflow](../.github/workflows/android.yml) runs source/host
checks, full ARM debug and engine-only builds, and the x86_64 app instrumentation
suite. It does not certify the game on Thor hardware or publish APKs.
