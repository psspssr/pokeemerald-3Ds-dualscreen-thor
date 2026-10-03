# Validation — Android previews

## 0.1.0-alpha.5 — controller pause and shiny odds

The release candidate adds **L1 + R1** for the app pause menu and five shiny
choices: **Original (1 in 8,192), about 1 in 2,048, 512, 256 or 128**. Existing
preferences retain their meaning; all boosts remain optional. The README now
contains a compact controller/keyboard command sheet.

All **74 app instrumentation tests** and release lint pass locally. Coverage
includes both display windows, Settings, individual shoulders, controller
identity, canceled events, held/repeated input, digital/analogue trigger
combinations, disabling fast-forward and the five-choice preference mapping.
Host checks and the native gameplay sanitizer suite also pass. The native
engine and data ABI remain `07329dad`.

Real ARM-game checks on API 30 used a kernel virtual controller, so events
passed through Android's input dispatcher. This reproduced a held-button bug:
after another key was pressed, Android could deliver a hardware repeat as a
new DOWN with fresh timestamps and repeat count zero. Shortcut guards now
require an observed release. Digital and analogue R2 release evidence are
tracked independently, including releases inside the pause menu and Settings.

The final matching-source probe passed separated and same-report held-repeat
replays for L1/R1 and R2, both trigger release orders in instrumentation, and
real mixed-trigger partial/full-release checks. Genuine releases rearmed the
shortcuts; held repeats did not reopen pause or flip fast-forward. The probe
APK SHA-256 is
`b79f4586a86e42a8dc0de6367cd779536d078045739620a3af10a7aaae86dfcb`.
Screenshots and test logs are recorded in `build/evidence/pause-odds/`.

The emulator display-progress test now allows a bounded wait for both
surfaces to advance, retaining its running-state, display-assignment and
pixel checks. It no longer treats a single slow software-rendering interval
as a hardware frame-rate requirement. Physical Thor, audible output and
sustained hardware performance remain unverified.

## 0.1.0-alpha.4 — full panels and gameplay repairs

The [alpha.4 release](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/releases/tag/v0.1.0-alpha.4)
was built and published automatically from immutable commit
`33986a45e9c64a5fe31f1e51ba97364cb4cdc65b`. All seven jobs passed on the first
attempt in [run 37112567002](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/actions/runs/37112567002):
**50 app tests, 79 tooling tests, 18 graphics host checks, 136 GLES pixel
assertions**, native/host/system sanitizer checks, production packaging,
isolated signing and verified upload.

| Published artifact | Verified value |
|---|---|
| Version | `0.1.0-alpha.4`, Android version code **4** |
| APK | `emerald-thor-0.1.0-alpha.4-armeabi-v7a.apk` |
| Bytes / SHA-256 | **27,534,476 bytes**; `1da875ff9a9144c84d503a723c27068c8fa0e77f0ab1a0d9e20c5e7204c7ef19` |
| Engine/data ABI | `07329dad` |
| Signing identity | Unchanged: `eeb95f89fcb944d3a62cc2aa8d0bb720584333d476c13b5a823ef486fdcd0389` |

Downloaded asset hashes, packaged native/data hashes, production package
metadata, signature and alignment match the manifest. All published assets
also match the immutable signed Actions artifact byte-for-byte. Evidence:
`build/evidence/release-alpha4-ci/verification.json`.

The exact downloaded APK updated over alpha.3 on API 30 with ARM translation.
The old raw save, Android preferences and port settings were unchanged at
installation and after the smoke test. Continue loaded the existing Oldale
adventure. Both full Thor rectangles were confirmed, and complete bottom-screen
gestures opened Party/Options, switched voxel rendering on/off and remained
responsive after an **8.14-second** gap. Home/resume, Map touch, display removal
with combined-layout controls, reattachment and subsequent Party touch passed.
Screenshots were inspected; no fatal, ANR or graphics-backend error appeared.
The inherited OBJ-window/BG-mosaic diagnostics remain. Evidence:
`build/evidence/published-alpha4-qa/report.json`.

This exact-artifact smoke did not repeat the isolated shared-EXP, move-learning
or evolution scenario. Those deeper checks used matching-source local builds
as documented below. Physical Thor and audible-output testing remain open.

### Local gameplay builds

The local candidates build the real ARM game with the same engine/data ABI.
Native compilation, debug/release packaging and release lint pass. The
release-signed development probe has SHA-256
`91021990258246c0b9e5b7083bd38623733c0d3d62368dd5423599dd4c9838f1`;
the matching debug probe is
`ee318d65b6ddbcbaef24f3d63338f01e7668a72515c64ad6144568ebaee95ee0`.
These probes support local gameplay checks and are distinct from the published APK.

Normal CI for the final game-code commit `bd3a95e101a426c0a4861d2976a0646fda943092`
passed all three jobs in [run 37111512498](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/actions/runs/37111512498),
including the new native evolution and Summary checks. Later candidate
commits contain documentation and presentation images only.

### Fullscreen display and input

Dual-display **Fill** now stretches each complete source image to its panel,
without cropping: **1920×1080 top** and **1240×1080 bottom**. **Fit** remains
available, including integer scaling. Phone layouts keep their existing
scaling. Optional automatic touch controls can be shown or hidden through the
pause menu even when Fill leaves no border to tap.

The local app suite passes **50 instrumentation tests** and lint, including
exact Thor rectangles, visible source corners, independent-axis touch mapping
at the extreme corners and centre, both display assignments, Fill/Fit,
rotation, resize, disconnect/reconnect and immersive window flags. CI uses a
smaller main display to reduce software-rendering load; both environments use
a 1240×1080 secondary display. **136 production GLES pixel assertions** check
both full Thor sizes, both screen sources and both filters, including the
outermost destination pixels. **79 tooling tests** also pass.

Real-game emulator checks confirmed full panels, Party/Options/Map touches,
voxel on/off, Fit borders, swapped displays with bottom touch on the primary
panel, Home/resume, optional controls and unplug fallback. The final signed
probe loaded the existing Oldale save after updating, with the save and both
settings files unchanged at installation. Evidence:
`build/evidence/fullscreen-sweep/` and `build/evidence/fullscreen-contract/`.

### Bugs reproduced and repaired

- Mystery gifts could be granted to a temporary Battle Pike or multi-partner
  party, then discarded when the normal party returned. Those challenges now
  block events; ordinary lobbies remain eligible. Native ASan/UBSan regressions
  exercise the actual party backup/restore routines and all seven event choices.
- The five-move selector indexed a tiny external-asset placeholder as a full
  tilemap; sliding panels also offset the placeholder before resolution.
  Both now resolve the real asset before indexing. External/embedded asset
  checks pass under ASan/UBSan with unchanged tilemap footprint and palette.
- Move replacement did not consume bottom-screen touches. Rows now preview
  the move; explicit **OK! / BACK** use the existing confirmation, HM-refusal
  and cancellation paths. Tests cover all rows, stale/fading/sliding input and
  unchanged controller behaviour. Live playtesting confirmed preview, BACK,
  touch confirmation, move replacement and return to the field. The original
  four-row Summary has source/sanitizer coverage in this sweep, not a new live
  PC Summary playthrough.
- Evolution mixed the 240-pixel GBA scene with 400-pixel rendering coordinates,
  leaving the Pokémon off-centre, an extra dialogue tile strip and particles
  offset from the Pokémon. Scene and particle code now share the centred GBA
  viewport. Regression tests use the actual make flags, four sparkle factories
  and spray callbacks; ASan/UBSan passes. The final debug probe's 1× animation
  replay captured 30 phases; reviewed frames show centred spiral/spray effects,
  the evolved Pokémon and clean dialogue edges.

The deep shared-EXP/evolution test uses a clearly labelled **isolated cloned
save fixture**, prepared through the engine's Pokémon setters. A benched
Torchic gained EXP, reached level 16, learned Peck, evolved into Combusken and
replaced Scratch with Double Kick. The resulting ordinary raw save validates.
It is test evidence, not natural progression or a marketing screenshot.
After replay, the original save, Android preferences and port settings were
restored byte-for-byte; all five original backups remained, with no fixture
backup inserted. Normal evolution was observed live; no live link trade was
performed.
Evidence: `build/evidence/qol-fixture/` and `build/evidence/summary-selector/`.

Independent app, renderer and gameplay reviews found no remaining blocker in
these changes. `origin/` still matches the pinned upstream tree, and native
SDK checks report **295 identifiers with zero missing declarations or linked
functions**. This sweep does not establish physical Thor performance, audible
output or full-game coverage. The inherited OBJ-window and nonzero-mosaic
limitations described below remain.

## 0.1.0-alpha.3 — bug fixes and renderer performance

The [alpha.3 release](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/releases/tag/v0.1.0-alpha.3)
was built and published automatically from immutable commit
`814a1a3eaa4ec3dad4de0065c483bf19406f2f59`. All seven jobs passed on the first
attempt in [run 37081479663](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/actions/runs/37081479663),
including **43 app tests, 77 tooling tests, 18 graphics host checks, 73 GLES
pixel assertions**, and the native/host/system sanitizer checks.

| Published artifact | Verified value |
|---|---|
| Version | `0.1.0-alpha.3`, Android version code **3** |
| APK | `emerald-thor-0.1.0-alpha.3-armeabi-v7a.apk` |
| Bytes / SHA-256 | **27,534,476 bytes**; `f584eb7cb60009c20076f588b81e19873ecb42127a5b6f9e4f0907738564bc00` |
| Engine/data ABI | `0076aa29` |
| Signing identity | Unchanged: `eeb95f89fcb944d3a62cc2aa8d0bb720584333d476c13b5a823ef486fdcd0389` |

Downloaded asset hashes, packaged native/data hashes, production package
metadata and signing identity all match the release manifest. The published
assets also match the immutable signed Actions artifact byte-for-byte.

The exact downloaded APK updated over alpha.2 on API 30 with ARM translation,
preserving the old save, Android preferences and voxel settings byte-for-byte.
Continue loaded that adventure. A 1240×1080 second display accepted complete
DOWN/MOVE/UP gestures and remained responsive after a **7.93-second** gap.
Party, Options, voxel on/off, Home/resume, subsequent Map touch and fallback
to both screens with controls on one display passed. Screenshots were inspected;
no fatal, ANR or graphics-backend error appeared. The save remained unchanged
through this smoke test. Evidence: `build/evidence/release-alpha3-ci/verification.json`
and `build/evidence/published-alpha3-qa/report.json`.

### Independent bug sweep

Independent app, gameplay/save and graphics reviews found and fixed four
reproducible defects:

- A held R2 could toggle fast-forward again after pausing without a full
  release, or rearm when a different controller reported neutral input.
  Release tracking now belongs to the controller that held the trigger.
- Holding the virtual circle pad at its center could let a physical stick
  steer the game. Touch ownership now remains active at zero displacement.
- Exporting to a path/provider alias of the live save could truncate it.
  A synced private recovery copy now protects open, partial-write and close
  failures; interrupted exports recover before imports. Failed restoration
  retains the copy and pauses gameplay until retry succeeds.
- Answering **No** to “Use next Pokémon?” could flee a living wild shiny
  without confirmation. **Stay** now opens required replacement selection
  before escape RNG, counters or outcome change.

The app passed **43 instrumentation tests** and lint. The storage regressions
use actual test document providers that fail after truncation, partial writes
and close; normal pipe exports still pass. Recovery was independently reviewed
against activity recreation, serialized file operations and native save writes.
Native ASan/UBSan tests compare the real fainted-escape command with upstream
across 128 disabled/confirmed escape cases and exercise cancellation, missing
UI, pause, required replacement and normal whole-party loss.

The renderer skips unchanged program uniforms, unused texture samplers and
unchanged vertex attribute setup. It still uploads changed vertex data and
checks CPU edits to active textures. **73 GLES pixel assertions** pass, including
direct uniform writes, program changes, all three texture units, reordered
and strided vertices, changed buffer addresses and 2D/3D transitions. The
18 graphics host checks and system-shim sanitizer tests also pass.

The full ARM build and regenerated QoL/Mystery Events tests passed. `origin/`
still matches its pinned upstream tree, and native SDK coverage reports no
undeclared or unresolved functions. Logs and before/after reproductions are
retained under `build/evidence/sweep/`.

### Controlled renderer comparison

The same stationary voxel Route 101 scene was measured before and after on
API 30 with ARM translation and SwiftShader. Other local emulators were paused.
The release-signed comparison APK updated over alpha.2 without changing save
bytes; its SHA-256 is
`9b675408c877a6ddea7a3be95b42d45792643b3cb1e886bf1a9ce36177106569`.

| Measurement | Alpha.2 | Optimized build |
|---|---:|---:|
| One display, presented FPS | 59.88 | 59.88 |
| One display, game-thread CPU per frame | 11.884 ms | 11.411 ms |
| Two displays, presented FPS | 43.39 | 45.84 |
| Two displays, game-thread CPU per frame | 16.446 ms | 15.530 ms |

These are one controlled emulator comparison, approximately **4–6% less
game-thread CPU per presented frame**. They do not establish sustained Thor
performance. The standalone draw benchmark confirms fewer GL calls and uniform
uploads, but its wall-time samples vary and do not show a universal speedup.
Full timing logs and capture conditions are in `build/evidence/sweep/gpu/`.

### Integrated gameplay and save checks

Development APK `c5cf0c7073697af9fe9ea230f7a2cd77b3e69fae2fa1be33d6bcb08aca4f0095`
(engine ABI `0076aa29`) continued the existing save after updating. Playtesting
covered Route 102, Bug Catcher Rick's two-Pokémon battle, a wild battle, benched
Wurmple/Celebi level-ups, and Torchic reaching level 10 and learning Ember.
The 4× toggle, reward panels and battle/field transitions were visually checked.

A normal save produced counter 15 and SHA-256
`b0e714ef009584797d088e7ab16767366f5e35c024fb2d3968e408ee09b867f4`.
Five valid raw backups remained. Export through the Android Downloads picker
matched the live save exactly and returned to gameplay; restarting and Continue
loaded Route 102 with fast-forward initially off. Original GBA Emerald in mGBA
loaded the exported 128 KiB data with all 400 party bytes identical. No test
memory edits staged this progression. Evidence: `build/evidence/sweep/playtest-report.json`.

Normal CI for source checkpoint `4ef4f36` passed
[run 37079925529](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/actions/runs/37079925529).
Physical Thor, audible output and full-game coverage remain open. The rare
shiny-faint escape fix was exercised through real-command/host tests, not an
observed wild shiny encounter.

A further natural playtest advanced benched Wurmple from level 4 to 5 and
learned Poison Sting, checked both complete stat panels and the move summary,
then completed Youngster Allen's two-opponent battle and switching prompt.
Jirachi and Celebi also leveled through shared EXP. The final normal save is
counter **17**, SHA-256
`dd7e72079ec2bb45bba36907b05934c0971d75ceac0e1c41fe55f07e364fab82`;
both slots validate and five backups remain. Evolution was not reached.
Evidence: `build/evidence/graphics-polish/sweep-progression-report.json`.

The inherited OBJ-window warning corresponds to the title-logo shine mask,
which upstream omits. The overworld-load mosaic warning also fires with a
1×1 mosaic, where no pixelation would be visible; actual nonzero mosaic effects
remain unsupported. These diagnostics come from unchanged upstream rendering
checks, not the GLES cache change. Their counter counts distinct warning
types, not occurrences. Still-image inspection does not validate those animated
effects, and the limitations remain documented below.

## 0.1.0-alpha.2 — Mystery Events and automatic releases

Seven individual choices now live under **Settings → Gameplay → Mystery events**:
Eon Ticket, Mystic Ticket, Aurora Ticket, Old Sea Map, offline Jirachi/Celebi
gifts, and missing Regi dolls. [Behavior and prerequisites](MYSTERY_EVENTS.md)
are documented alongside [actual app screenshots](images/mystery-events-captures.json).

The complete local ARM build activated all seven through the real menu. Its
normal save/restart retained the rewards and correctly changed their status
without duplicate claims. The title screen reported no loaded game, a native
Bag menu blocked changes, and a normal idle overworld allowed them. No new
crash or EGL error appeared. This development APK was
`76e864800ff2b4808e05a8f7e7e2e9b5915ac03531fe0c6e49cf502d59c80564`, engine ABI
`e04e5ad0`, built from feature checkpoint `3f0a2ca`.

The saved result was **128 KiB**, counter **13**, SHA-256:

```text
2a39cc8841256bb05c80c0edff4bb764d1d5881f4359c43c5e16385bf8de6269
```

It contained one of each ticket, the matching access/receipt flags, both
level-5 gifts, and one of each Regi doll. The original two party records and
Champion/encounter-completion flags stayed unchanged. The disk save did not
change until an ordinary in-game Save. Original GBA Emerald in mGBA loaded the
result with **all 400 party bytes identical**; Party and both gift Summary
screens were inspected. No game-memory or save edits staged these rewards.
Evidence: `build/evidence/mystery-events/{runtime-report,gba-compatibility}.json`
and the screenshots/logs in that directory.

Native ASan/UBSan tests execute the actual engine inventory, flags, Pokémon
creation, ScriptGiveMon/Pokédex and decoration routines. They cover partial
imported ticket state, PC ownership, completed encounters, full inventories,
gift deduplication and failed decoration insertion rollback. Host tests cover
paused-thread execution, no frame advancement, queued expiration/no late
activation, concurrent calls and exact completion of an already-started action.
App tests cover descriptions/statuses, blocked actions, repeated taps, rotation,
queued departure and error handling.

The normal CI checkpoint `e70e23c` passed [run 37049133779](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/actions/runs/37049133779).
Publishing [alpha.2](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/releases/tag/v0.1.0-alpha.2)
then triggered the new release workflow automatically against immutable tag
commit `6dda8f71557b7e479c8ac1f0ad3c61ee314a7992`.

**All seven release jobs passed on the first attempt** in [run 37050988950](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/actions/runs/37050988950).
The release event assigned version code **2**, reran the **31 app tests, 77
tooling tests, 18 host-graphics checks and 57 GLES assertions**, and completed
the native/engine sanitizer checks. Build, isolated signing and verified upload
then succeeded. The published assets match the retained signed Actions artifact
and independently downloaded checksums, package metadata and signer.

| Published artifact | Verified value |
|---|---|
| Version | `0.1.0-alpha.2`, Android version code **2** |
| APK | `emerald-thor-0.1.0-alpha.2-armeabi-v7a.apk` |
| Bytes / SHA-256 | **27,534,476 bytes**; `44ecfaabe01780c0a228b8e867eab6cb862118798ced7bc74a23dcbe2a386a8b` |
| Engine/data ABI | `e04e5ad0` |
| Signing identity | Same certificate as alpha.1: `eeb95f89fcb944d3a62cc2aa8d0bb720584333d476c13b5a823ef486fdcd0389` |

The exact downloaded CI-published APK was installed on API30 with ARM
translation. Its same-certificate update preserved the alpha.1 save and
preferences byte-for-byte, and Continue loaded that old adventure. A separate
copy of the pre-event fixture then activated Aurora Ticket, saved normally,
restarted, and correctly refused a duplicate. The resulting counter-13 save
remained a valid 128 KiB file with only the ticket/access/receipt changes and
no Champion flag change.

A 1240×1080 secondary display accepted full DOWN/MOVE/UP Party gestures and
remained responsive after a **7.981-second** gap. Options, Home/resume, subsequent
Map touch and removal back to combined layout/controls passed. No fatal or ANR
entry appeared, and the on-device APK hash still matched after the checks.
The screenshots were visually inspected. Evidence:
`build/evidence/published-alpha2-qa/report.json` and its captures/logs.

The release workflow itself checks only its build and automated test coverage;
maintainer gameplay acceptance above is a separate check of the published APK.
Its manifest does not claim an unperformed hardware or full-game test. Release
notes/checksums and the public build manifest accompany the APK.

These tests used an early-game save. Island voyages and legendary fights were
not played through; normal Champion/story requirements were preserved. Full
inventories, partial imported states and completion variants have engine-test
coverage. Physical Thor behavior, sustained hardware performance and audible
output remain untested.

## Earlier preview — 0.1.0-alpha.1

The ARM Android game runs through the opening, starter choice, wild battles,
the first rival battle, level-ups and ordinary save/load. The bottom-screen
window has been exercised on a separate 1240×1080 emulator display. This report
identifies the first signed preview and distinguishes automated checks,
real-game checks and hardware work still outstanding.

### Release identity

| Item | Verified value |
|---|---|
| Release | [0.1.0-alpha.1](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/releases/tag/v0.1.0-alpha.1) |
| Code checkpoint | `5f5666b75ead74e5f896a1a194560d7295014857` |
| Imported 3DS project | `ZallaxDev/pokeemerald-3Ds-dualscreen` at `4c64da2644223668c60e59c7731347baada7295d` |
| Unchanged `origin/` tree | `6d909815a5ffaede2e8f31ea0df5d08a6acee73a` |
| pret/pokeemerald pin | `76463dac15cad36aca5e2b3f6366abf9e53f814f` |
| APK | `emerald-thor-0.1.0-alpha.1-armeabi-v7a.apk`, **27,460,748 bytes** |
| Application | `com.emerald3ds.android`, version `0.1.0-alpha.1`, version code **1**, non-debuggable |
| Requirements | Android 9/API 28+, OpenGL ES 3, **32-bit ARM app support** (`armeabi-v7a`) |
| Engine/data ABI | `c27ba49a` |

APK SHA-256:

```text
b8f46a521e93287288dac7312515893cd12db89f7c9efe66b2d4bd493dc9ba98
```

Signer certificate SHA-256:

```text
eeb95f89fcb944d3a62cc2aa8d0bb720584333d476c13b5a823ef486fdcd0389
```

APK signature and alignment verification passed. Both packaged native libraries
were byte-compared with the final native outputs, along with the embedded-data
marker, engine ABI, bottom-menu asset and voxel shader. The preview includes
matching data. Release assets include its checksum and a machine-readable build
record; signing secrets stay outside the repository.

### Automated checks

**All three jobs passed** in [CI run 37040555915](https://github.com/psspssr/pokeemerald-3Ds-dualscreen-thor/actions/runs/37040555915)
at the exact code checkpoint above. The workflow builds both native packaging
variants and runs the source, host, engine, app and GLES checks below.

| Check | Result and boundary |
|---|---|
| Native builds | Full ARM game, fixed-address loader, strict undefined-symbol/link checks and engine-only package passed. Gradle release build and lint passed. |
| App instrumentation | **27 tests** cover layouts, independent bottom-window input, removal/reconnect, settings, save import/export, backup ordering/restore and the real JNI shiny-confirmation dialog. Runs use the separate harness, not the full game. |
| Android GLES | **57 pixel assertions**, plus fast-forward scheduling/resume checks, cover 2D/voxel shaders, blending, texture views, framebuffer ownership and mixed CPU/GPU output. |
| Host graphics | **18 tests** cover textures, shaders, pacing and fast-forward scheduling. |
| Source/tooling | **64 tests** cover upstream update safety, strict ordered patches, save validation, link diagnostics, backups and the visual overlays. |
| System and host | ASan/UBSan checks passed for files, memory, input, timing, locks, audio, option defaults and confirmation lifecycle. |
| Game rules | ASan/UBSan tests execute the actual patched Pokémon creation/encryption and RNG code. Disabled options preserve the original 100-byte Pokémon data; boosted fixtures retain valid encryption/checksums, nature, gender, ability, moves, stats and RNG progression. Shared-EXP rules cover 256 eligibility combinations. |

The shiny tests exercise all 65,536 XOR values and 4,096 constrained personality
fixtures. Incompatible personalities safely retain their original value.
The upstream signed-byte-shift idiom has a narrow sanitizer exclusion; the new
rule implementation remains instrumented. Save-backup tests cover incomplete
writes, failed snapshots, retained unknown files and rotation.

### Real-game checks

The exact signed APK above was installed and its on-device bytes verified on
API 30. It passed Continue, an ordinary Route 101 battle, 1240×1080 secondary
Party/Options gestures including MOVE and a 7.47-second gap, Home/resume,
display removal, fallback and reattachment. All QoL options remained off.
The same-key update preserved the baseline save byte-for-byte; no ANR or fatal
entry appeared. Evidence: `signed-release-qa/final-b8f46a/report.json` and its
captures, package metadata and logs.

The full ARM game runs on an API 30 emulator with ARM translation and
SwiftShader. Regression checks span the recorded development checkpoints;
the final native library was rechecked through capture, nickname entry and a
subsequent battle, then the signed APK was checked separately after packaging.
Screenshots were inspected for actual scene and UI rendering, not just process
survival.

| Area | Observed result | Local evidence under `build/evidence/` |
|---|---|---|
| Progression and battles | Introduction, clock/May/Birch events, starter, wild battles, normal blackout recovery and victory over May worked. Torchic reached level 8; Pokédex, Poké Balls and running shoes were obtained normally. | `qol-*` screenshots, `graphics-polish/post-rival-pokedex.{sav,json}` |
| Field graphics | Textured voxel buildings, map/menus and 2D/voxel transitions rendered correctly. Location banners on Route 101 and Littleroot no longer repeat along the bottom of the taller viewport. | `qol-banner-*.png` |
| Level-up graphics | At checkpoint `2cd3605`, a saved level-6 Torchic defeated an ordinary wild Wurmple and reached level 7. Both stat-increment and stat-total pages show all six rows above the unchanged message; ordinary battle sprites and status bars remain correct. | `qol-025-stat-{increments,totals}.png` |
| Caught-Pokémon registration | The complete description and lower border remain visible through the registration page. Nickname entry, backspace and confirmation worked; the game returned to the field and completed a subsequent wild battle with normal scaling. | `qol-026-*.png`, `graphics-polish/026-capture-runtime-report.json` |
| Fast-forward | The selector, R2 toggle and L2 hold worked. Normal-speed return and Bag-to-world transitions passed. Audio queues are muted/flushed during acceleration. | `graphics-polish/fast-forward-rates.json`, `qol-l2-*.png` |
| Backups | Six normal saves retained exactly five complete 128 KiB snapshots. Restoring the oldest through Settings/Restart reproduced its exact bytes and kept the displaced active save as `.bak`. | `graphics-polish/backup-{retention,restore}-report.json` |
| Shared EXP | Two ordinary level-3 Poochyena battles awarded Torchic 23 EXP each. Benched Wurmple gained 0 EXP with the option off and 11 with it on; no switching or held item was involved. Normal saves and all Pokémon checksums passed. | `graphics-polish/shared-experience-runtime.json` |
| Audio | The real game opened a 44,100 Hz AAudio stream; the PCM probe produced nonzero samples. The emulator ran with host audio disabled, so sound quality was not auditioned. | `vanilla-save-in-android.log`, `native-audio-probe.log` |
| Launcher | Adaptive icon built and inspected in the Android launcher. | `launcher-icon.png`; [artwork provenance](ICON.md) |

A rare shiny encounter was not required for the runtime playthrough. Its
personality rules and encounter/escape hooks have deterministic native coverage;
the actual Android confirmation dialog, default Stay action and lifecycle
cancellation have JNI/instrumentation coverage. Full-game encounter variants
still need playtesting.

### Graphics repairs and upstream boundary

The port now handles streamed texture views without losing building materials,
retains CPU redraws after GPU menu output, requests unbuffered secondary-touch
dispatch to avoid a reproduced API 30 drag ANR, and avoids extra frame waits
after rendering overruns. Integer scaling also keeps both screen dimensions
integral in the top-large layout.

Narrow generated-source overlays fix field-banner wrapping, level-up panel
clipping and the caught-Pokémon registration viewport. The imported `origin/` tree remains byte-for-byte unchanged.
The current upstream has a voxel **overworld**, with transitions to normal 2D
battles; it does not contain voxel battles to enable. Unsupported shader
instructions fail the build rather than silently translating incorrectly.

The backend targets this pin's straight-line PICA shaders and uncompressed
textures. It does not implement every 3DS graphics feature. Upstream OBJ-window
and BG-mosaic limitations remain; targeted pixel tests do not establish
rendering correctness for every later-game scene.

### Performance

These are software-emulator measurements, not AYN Thor benchmarks. The host
uses KVM, ARM translation and SwiftShader; no hardware graphics driver is bound.

| Selected speed | Measured game ticks/second in the tested scene |
|---|---:|
| Normal | 59.79–59.83 |
| 2× | 119.64–119.66 |
| 4× | 239.23–239.43 |
| L2 held at 4× | 239.30 |

Fast-forward advances logic while retaining the 59.83 Hz presentation target.
It does not ask either display to render 240 frames per second. Attainable
speed depends on the device and scene.

Earlier full-game single-display 2D and warm voxel scenes reached approximately
59.3–59.9 FPS. Dual voxel samples were 39.8–43.4 FPS; a slower dual-2D interval
was 20.5–27.3 FPS and recovered after surface recreation. The corrected pacing
trace eliminated unnecessary sleeps of at least 15 ms, but rendering/resource
state varied, so its FPS change is not an isolated benchmark. Sustained
full-speed dual rendering still requires a physical-device measurement.
Evidence: `performance-followup.json`, `dual-2d-*-summary.json` and
`graphics-polish/fast-forward-rates.json`.

### GBA save compatibility

The original GBA Emerald was built from the pinned source (ROM SHA-1
`f3ae088181bf583e55daf962a92bb46f4f1d07b7`) and run in **mGBA 0.10.2**.
Android-created saves loaded in that game; the GBA game saved again and Android
imported, restarted, loaded and exported them through the system document
picker. The 131,072-byte GBA result and Android import/export were identical:

```text
9e64b0d22a882c267641e4c78e577dcdf7f595616b1b01912cb6a067d197cba4
```

A separate GBA-created player also loaded and saved on Android. The natural
post-starter Android save was checked in the original game: player A's level-5
Torchic retained its Adamant nature, Blaze ability and Route 101 origin:

```text
aa7afc7c1e164ab1b1a54d381f2f1f3fbb83a7d4242c5879df10a814959295c0
```

After the QoL changes, the normal two-party save from the shared-EXP test also
loaded in the original GBA game. Its complete 200 bytes of party data matched
the Android save. Party and Summary showed level-2 Wurmple with 19 EXP and
8 EXP to the next level, including the 11 EXP earned through the option.
This 128 KiB save, counter 12, has SHA-256:

```text
b902ea5500be79a5c3ade4e55895f3d7b129880f4101446821ecd5ba34330f6a
```

Evidence: `graphics-polish/qol-gba-compatibility.json`, `qol-gba-runtime.log`,
`qol-gba-party.png` and `qol-gba-bench-summary.png` in the same folder.
App/native tests also cover the final importer, backup validation and Pokémon
serialization. mGBA's
optional 16-byte RTC trailer is normalized on import. Exports have no Android
header or RTC trailer, and port preferences remain separate. Emulator save
states and clock overrides are not transferred. See [save transfer instructions](BUILDING.md#engine-only-build-and-data-packs).

Evidence: `android-gba-roundtrip.json`, `gba-reference-save.json`,
`vanilla-resaved-by-android.json`, `roundtrip-app-export.sav`,
`gameplay-torchic-android.{sav,json}` and `gameplay-torchic-gba-*.png`.

### Coverage still open

- No physical Thor was available. Firmware ABI support, lid behavior, panel
  timing, sustained performance and thermals require hardware testing.
- This is opening-game coverage, not a complete playthrough. Later battles,
  PokéNav/PC flows and long sessions need broader playtesting.
- Audible output has not been assessed; stream and PCM tests do not substitute
  for listening on a device.

Local logs, save fixtures, ROMs and full test captures remain under ignored
`build/evidence/`. The repository publishes a curated set of unmodified
[game screenshots](images/README.md), source, test results and release identity.
No ROM or save fixtures are attached to the release.

### Repeat the checks

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
bash android/gpu/test/run-emulator.sh emulator-5584
ANDROID_SERIAL=emulator-5584 android/app/gradlew -p android/app connectedHarnessAndroidTest lintHarness
```

Select the intended connected emulator serial. Use the [runtime acceptance
checklist](BUILDING.md#runtime-acceptance-after-an-upstream-update) after an
[upstream update](UPDATING_FROM_ORIGIN.md), and the [release guide](RELEASING.md)
when signing and publishing a new APK.
