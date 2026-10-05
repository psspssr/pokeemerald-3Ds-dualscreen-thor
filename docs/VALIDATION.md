# Validation

## Beta development checkpoint

The candidate at `f7bf78f` imports upstream `c330c0a1aece` and uses engine/data
ABI `1e406ec1`. Its native library SHA-256 is
`705ebc0ddefd17b9a8b5ebf57093ef6cbd3b0375fe341487e62d59868945428f`.
The nondebuggable, release-signed APK was installed without changing the save
or either settings file. Release packaging will record its own source and
artifact identity; the data ABI alone does not identify executable bytes.
Runtime coverage below spans the identified candidates, rather than implying
that every earlier route was repeated after every change.

| Local check | Result |
|---|---|
| Android app/input/storage/display tests | 133 passed; harness and release lint passed |
| Build, updater, saves, overlays and website tooling | 105 passed |
| Production GLES pixel checks | 370 assertions passed |
| GPU host checks | 21 passed |
| ARM game build and SDK coverage | Full link passed; 321 identifiers, no missing APIs |
| Native regressions | 22 suites passed, including gameplay, assets, menu geometry, save layout and real thread/shutdown checks |

The menu pass fixes stale Bag-grid selection state, shop metatiles, naming
touch input, Contest/Pokéblock/berry layouts, Contest graphics and hidden
hearts leaking into the wider picture. Related source tests also repaired
evolution/transition graphics, drought palettes, Battle Factory backgrounds,
Mirage Tower/fossil buffers and Spinda spots. Later facilities have host-side
coverage; they have not all been reached during a natural playthrough.

Actual Android 11 playtesting reproduced an intermittent secondary-window
input freeze after Settings and the document picker. Keeping controller focus
on the main activity passed three repetitions, cold start, display recreation,
held-controller input and a five-minute gap between secondary touches. The
combined nondebuggable candidate also passed the previously failing picker
route and the first touch after recreation. This is bounded evidence, not a
claim about every firmware version.

Matched menu builds were played through protagonist naming on all three
keyboard pages, all three centered starter previews and cancellation,
Pokéblock feeding, HM replacement refusal and a complete five-appeal Contest.
The later nondebuggable candidate also passed corrected Condition colors,
hidden Contest hearts, Shop buy/sell cancellation, Bag-to-PC Summary/Mark,
deposit/withdrawal, touch box naming, a natural capture and touch Pokémon
naming. A normal save reloaded at the same PC with the expected party. Extra
Player PC checks withdrew and redeposited an item, canceled a toss without
losing it, and handled an empty mailbox.

That extra pass also reproduced an ignored Pokédex OK button before National
mode is unlocked: its drawn position differed from its touch target. Overlay
097 aligns the Hoenn hitbox and disables the former invisible target. The
regression exercises the actual drawing and action handlers for Hoenn and
National layouts, including SEARCH and SHIFT. An independent review also
checked conditional Options rows, Summary move slots and PokéNav layouts.
The rebuilt nondebuggable APK passed Fire→Torchic and no-result searches
through the visible OK button, ignored the old hidden hit area, and accepted
SHIFT's alphabetical order before returning to Map. Filter popups and their
cancel paths were exercised; National geometry has source-level coverage.

Natural Route 102 play covered classic/voxel switching, 1×–4× resolution,
anti-aliasing, battle Bag/Party, an animated attack, knockout, EXP and field
return. Actual screenshots were inspected at every quality level. New or
missing resolution preferences now start at **2× Sharp**, with AA off;
explicit stored choices remain unchanged. During short, stationary software
emulator samples, 1× averaged 58.5 presentations/s and 2× 55.2; 3×/4× cost
more. These measurements informed the default but do not predict Thor rates.
The renderer lowers quality after resource/allocation failure, not low FPS.

This playtest exposed the console's 6 MiB graphics-backing limit: some menu
and battle transitions fell back to slower tile drawing or unfiltered zoom.
Android now has a bounded, on-demand 16 MiB quota. Sanitizer tests cover
exhaustion, fragmentation, bank offsets, repeated reuse and real heap failure;
the `86aad4c` APK then passed three field cycles and three real battle-menu
cycles with no allocation fallback. Observed peak usage was 6,564,864 bytes,
above the old quota; settled battle use returned exactly to 5,362,688 bytes.
Total emulator native heap still grows, consistent with earlier EGL-emulation
traces; this is not a claim that aggregate heap use or physical-device memory
has been validated. The later Pokédex change does not alter graphics allocation.

On the earlier `03b48fc` candidate (APK SHA-256
`4820405ab034664530058b543c99f1245b3869b52c870b6514f6f4c68eda4b5e`,
native library `c760ebb3210df419d7d4078bb995056faa390b098224c4cf27b8bf6b7e294ec1`),
a normal in-game save advanced counter 19→20, produced a valid 128 KiB raw
Emerald save, and exported byte-for-byte through Android's document picker.
Original GBA Emerald loaded that export in mGBA, with all 400 party bytes
unchanged. Returning from the picker retained working Party/Map touch input.
Import validation now rejects corrupt active sectors before replacing the
current save or backup; a corrupt pending import can be quarantined and retried.
The later VRAM change does not alter save code; its runtime acceptance remains
separate from this save/export check.

Coverage is deliberately bounded. Frontier challenge menus, link desks,
the NPC Berry Blender, ribbon/painting details, every Pokédex filter combination,
and every storage-item route were not played in this pass. Source-level tests
cover the repaired Frontier/asset paths. No full-game completion is claimed.

The original QA saves and settings are restored after each owned run. Physical
Thor lid behavior, audible audio, sustained performance, thermals and battery use still
need the [hardware pass](testing/HARDWARE_TEST.md). Existing releases retain
the historical validation below.

## Previous preview: 0.1.0-alpha.8

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
python3 android/native/test/run_pokedex_search_tests.py
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
