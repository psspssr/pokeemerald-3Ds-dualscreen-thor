# Upstream 0.5.0 Android integration — completed local handoff

Completed locally on 2026-11-02. This is a validated **local integration**, not a published release. Work remains in `/root/fitme/pokeemerald-thor`, branch `main`. No push, signing or site publication occurred.

## Source identity and boundaries

The Android remote was already current at `8deaeed`. The clean updater imported ZallaxDev `main` at `bcae4b70dac61419fc933084084469f6cfbe344b` (0.5.0), replacing `c330c0a1aece35966b2d899013d16893fe70cff5` (0.2.0).

The updater created local commits `7d77f3b` (squashed upstream), `4f19124` (subtree import), and `ab9be72` (origin.lock). `origin/` remains an unmodified upstream subtree. Android adaptations are in `android/`, `tools/`, and ordered `patches/android/`; generated `build/upstream/` was not edited as source.

Upstream's original 0.5.0 port material is CC BY-NC-SA 4.0 within the scope of `origin/LICENSE-PORT.md`. Independent Android implementation code remains MIT. NOTICE, README and updating/building documents describe those scopes and the English-only build.

## Integrated changes

- Rebased the Android patch series and removed overlapping upstream fixes. Preserved Android touch guards, naming keyboard, save safeguards/backups, mystery events, storage recovery, sharp/MSAA/resolution behavior, asset-worker shutdown and voxel work-before-pacing.
- Retained the redesigned native bottom interface and whole-party routing. Added two-program shader containers and translated the weather entry point.
- Added Citro2D primitives, current graphics queue/cache compatibility, profiler-only CFG/firmware behavior, Home hint state, framebuffer-aware full-buffer copies and all five framebuffer formats. Partial copies explicitly fail.
- Included upstream soft-reset data/BSS ranges and generated `build/linkid.o` in both links. Replaced Nintendo UDS with the explicit unavailable Android adapter; no simulated peers or radio multiplayer.
- Kept upstream EXP sharing and shiny policy from duplicating Android bonuses. Added actual `CtrFieldView` scoring/eviction coverage in flat and voxel sanitizer builds.
- Corrected voxel target selection so native Pokédex `down && !bottom` frames do not select voxel scaling, while held-top behavior is preserved.
- START+SELECT held before L1+R1 reaches native soft reset without opening Android pause; L1+R1 alone still pauses.
- Overlay 103 preserves popup bounds, Spinda's embedded fallback and Android filtering after temporary upstream scene changes.
- Overlay 104 uses the established per-tile classic sprite path on Android. Upstream's new fast sprite batch produced black title text and player/NPC rectangles in live GLES runs.
- Overlay 106 uses Clang direct setjmp/longjmp builtins on Android. Both Bionic variants crashed in a minimal ARM-translation probe and during the actual reset; the builtin probe and game reset pass.
- API 30 test injection now reflects on `MotionEvent.setDisplayId`. Presentation resource metrics are synchronized before validation, preventing resize recreation storms and Binder exhaustion.

## Final automated results

Evidence is under ignored `build/evidence/upstream-20261102/`; older failure inventories and intermediate logs are not the final status.

- All **26 native test scripts** passed on one final source snapshot, including object eviction, asset loaders, Factory palettes and upstream field-view behavior.
- **99 tooling tests**, **26 GPU host tests**, and both shim/application host suites passed.
- All **155 connected app instrumentation tests** and full release lint passed on the API 30 emulator, including secondary display recreation and soft-reset chord ordering.
- **381 GLES pixel assertions** passed, including weather shader compilation, two-program containers, texture encode/decode, held-top framebuffer round trip and partial-copy rejection.
- Upstream randomizer and Nuzlocke host tests passed. Origin verification passed and all **384 SDK identifiers** have declaration and linked-symbol coverage.
- Final debug and engine-only release builds passed, including `lintVitalRelease`.
- The optional regional-locale staging/hash test does not pass on Android-overlaid English sources. Regional-ROM staging is not integrated or claimed; the build is English-only.

Final artifacts:

- Debug: `android/app/build/outputs/apk/debug/emerald3ds-android-debug.apk`, 33,983,912 bytes, SHA-256 `e35e051e456c3ee3ebbe7e8da3416bd58aec12e7a80282a23e3ab9d25a6acade`.
- Unsigned engine-only release: `android/app/build/outputs/apk/release/emerald3ds-android-release-unsigned.apk`, 20,143,973 bytes, SHA-256 `21b2ab3ca66d0c2fed03bcd2dea36d69151b7035746a5d9c607b212e19f2a668`.

## Real-game acceptance

The API 30 ARM-translation emulator used a copied valid 128 KiB counter-16 save and a 1240×1080 secondary Presentation. CONTINUE loaded with two party Pokémon. Party, Bag, native Pokédex list/info and top-picture restoration, native Options, pause/resume, voxel mode and camera changes were exercised with inspected captures.

A real Route 101 Zigzagoon battle rendered with correct classic sprites. FIGHT→SCRATCH updated HP, the battle ended in victory, shared EXP/level-up completed and play returned to the field. The sprite and soft-reset fixes were then rebuilt without diagnostics and rechecked.

Nuzlocke's tracker opened Route 101, displayed party state and level cap, recorded CAUGHT and persisted a 1436-byte companion file on normal save. Random Wild was toggled and restored OFF. Normal save counter 16→17 passed the independent GBA validator. The Android picker exported identical counter-17 bytes and imported them again; restart loaded the save. The final build completed START+SELECT→L+R soft reset and Continue without exiting. Original save, settings and both preference files were restored with matching hashes.

These are bounded emulator checks, not a full playthrough. Physical Thor behavior, sustained performance, audible audio, lid sensing, radio multiplayer, regional languages and unvisited weather scenarios remain unverified or unsupported. See `docs/VALIDATION.md` for the final artifact hashes, test counts and limitations in release form.
