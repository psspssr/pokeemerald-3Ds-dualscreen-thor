# Gameplay screenshots

These are unmodified screenshots of the actual Android ARM game and app,
captured on 2026-10-02. Each capture record identifies its development build;
[VALIDATION.md](../VALIDATION.md) records the tested coverage.
The main gallery uses the app's combined landscape layout at 1920×1080 with
on-screen gamepad controls hidden. The optional portrait view is 1080×1920 with
touch controls visible. The small FPS counter belongs to upstream's development
build.

The captures show an Android emulator, not physical Thor hardware. The separate
bottom-display window is covered by the runtime and instrumentation checks in
the validation report. Gameplay followed normal inputs; no progression or
memory edits were used to stage these scenes.

| File | Scene |
|---|---|
| [voxel-world.png](voxel-world.png) | Textured Littleroot buildings and trees beside the bottom-screen map. |
| [battle.png](battle.png) | Torchic encounters Poochyena on Route 101, with touch battle commands. |
| [party-summary.png](party-summary.png) | Torchic's stats, ability and moves beside Professor Birch's lab. |
| [portrait-controls.png](portrait-controls.png) | Voxel Route 101, the bottom-screen map and the phone's on-screen controls. |
| [mystery-events.png](mystery-events.png) | Seven event actions with real native availability in a saved adventure. |
| [mystery-deoxys.png](mystery-deoxys.png) | The Aurora Ticket description and individual activation dialog. |

[Gameplay metadata](captures.json) and [Mystery Events metadata](mystery-events-captures.json)
record the code revision, APK checksum and image checksums for each group.
The Mystery Events captures use portrait Android Settings at 1080×1920 and
the real native event backend. Local QA originals and full test evidence remain under the
ignored `build/evidence/` directory.

Pokémon game artwork shown in these screenshots belongs to its respective
rights holders. These screenshots are not relicensed under the Android port's
MIT code licence. See the project's [credits and licensing](../../README.md#credits-and-licensing).
