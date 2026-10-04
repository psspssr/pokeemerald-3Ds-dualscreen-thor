# README images

## Presentation mockup

[thor-presentation.png](thor-presentation.png) is a presentation mockup using
emulator captures, created with the built-in `image_gen` editor. It combines the
[DROIX-hosted AYN Thor product image](https://droix.net/wp-content/uploads/2025/08/AYN-THOR-BLACK-LISTING-DONE-01.png)
with the gameplay and touch-screen regions of [classic-town.png](classic-town.png).
It illustrates the two-panel presentation and is not evidence of a physical
hardware test. Generative editing can alter fine artwork or text details; the
unmodified emulator captures below remain the source for actual game appearance.

The complete device, both screen contents and all eight touch-menu rows are
visible. The PNG retains transparency. [Generation provenance](thor-presentation.json)
records the prompts, input and output hashes, source credit, and selected edit.
The device reference and Pokémon artwork remain subject to their owners' rights;
the port's MIT code licence does not relicense them.

## Display and speed settings

[voxel-antialiasing.png](voxel-antialiasing.png) and
[fast-forward-speed.png](fast-forward-speed.png) are unmodified captures of
actual Android Settings on the release-signed `3019b32` candidate used to
validate alpha.8. They show Off/2×/4× anti-aliasing and 2×/4×/8× fast-forward.
Fast-forward controls were enabled for that demonstration; optional features
still start off. [Exact build and image checksums](display-options-captures.json).
These are emulator captures, not physical Thor photographs.

## Gameplay screenshots

[shiny-odds.png](shiny-odds.png) shows the actual five-choice Android picker,
with original odds selected. It is an unedited capture from the local signed
alpha.5 development probe. [Build and capture details](shiny-odds-capture.json).

These are unmodified screenshots of the actual Android ARM game and app,
captured on 2026-10-02 and 2026-10-03. Each capture record identifies its build;
[VALIDATION.md](../VALIDATION.md) records the tested coverage.
The main gallery uses the app's combined landscape layout at 1920×1080 with
on-screen gamepad controls hidden. The optional portrait view is 1080×1920 with
touch controls visible. These older captures retain the then-fixed FPS counter. Current previews show it only when **OPTION → SHOW FPS** is enabled.

The captures show an Android emulator, not physical Thor hardware. The separate
bottom-display window is covered by the runtime and instrumentation checks in
the validation report. Gameplay followed normal inputs; no progression or
memory edits were used to stage these scenes.

| File | Scene |
|---|---|
| [classic-town.png](classic-town.png) | Original 2D Oldale Town and Pokémon Center with the bottom-screen map; voxel rendering off. |
| [classic-route.png](classic-route.png) | Original 2D Route 101, trees, grass and ledges; voxel rendering off. |
| [voxel-world.png](voxel-world.png) | Textured Littleroot buildings and trees beside the bottom-screen map. |
| [battle.png](battle.png) | Torchic encounters Poochyena on Route 101, with touch battle commands. |
| [party-summary.png](party-summary.png) | Torchic's stats, ability and moves beside Professor Birch's lab. |
| [portrait-controls.png](portrait-controls.png) | Voxel Route 101, the bottom-screen map and the phone's on-screen controls. |
| [mystery-events.png](mystery-events.png) | Seven event actions with real native availability in a saved adventure. |
| [mystery-deoxys.png](mystery-deoxys.png) | The Aurora Ticket description and individual activation dialog. |

[Classic 2D metadata](classic-captures.json), [earlier gameplay metadata](captures.json)
and [Mystery Events metadata](mystery-events-captures.json)
record the code revision, APK checksum and image checksums for each group.
The classic 2D captures use the exact signed alpha.3 release APK.
The Mystery Events captures use portrait Android Settings at 1080×1920 and
the real native event backend. Local QA originals and full test evidence remain under the
ignored `build/evidence/` directory.

Pokémon game artwork shown in these screenshots belongs to its respective
rights holders. These screenshots are not relicensed under the Android port's
MIT code licence. See the project's [credits and licensing](../../README.md#credits-and-licensing).
