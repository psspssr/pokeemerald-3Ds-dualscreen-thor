# Screenshots

Gameplay and settings images are unmodified captures from the Android port
running in an emulator. The device view is an illustrative mockup. Full physical
Thor validation and sustained performance testing remain open; see
[test results and limits](../VALIDATION.md).

## Settings in beta.4

[Settings home](settings-home-beta4.png) shows the five categories and controller
focus on Display. It is an unmodified 1920×1080 capture from the exact published
beta.4 APK (`31e9c92`, engine/data ABI `a198e6bc`). Image SHA-256:
`fa07c2e7e865722cfe32e7ce7cc98e47bad58fabd0bd106cdc5ace387d02a744`.

## Display and speed settings

[Voxel anti-aliasing](voxel-antialiasing.png) and
[fast-forward speed](fast-forward-speed.png) show the release-signed candidate
used to validate alpha.8. Supported anti-aliasing choices are Off, 2× and 4×;
fast-forward offers 2×, 4× and 8×. Fast-forward was enabled for the screenshot.
Optional features still start off.

## Beta gameplay captures

The beta gallery shows separate 1920×1080 and 1240×1080 emulator displays,
with Fill scaling and the FPS counter off. These are actual captures, not
upscaled or retouched artwork. The 2× and 4× images use the same Route 102
save and camera; ambient animation and NPCs continue between captures.

| Image | Scene |
|---|---|
| [Classic route](classic-route-beta.png) | Route 102 with voxel rendering disabled. |
| [2× Sharp](voxel-sharp-2x.png) | Voxel Route 102 at 800×480 internal resolution, blur and anti-aliasing off. |
| [4× Ultra](voxel-ultra-4x.png) | The same view at 1600×960 internal resolution. |
| [Voxel battle](voxel-battle-beta.png) | Natural Wurmple encounter on Route 101 with Sharp sprites and text. |
| [Battle touch screen](thor-battle-touch.png) | Torchic's move selector filling the lower panel. |
| [Touch keyboard](thor-touch-keyboard.png) | Protagonist name entered by finger on the lower panel. |

The battle and battle-touch images come from the exact signed **beta.2** APK,
with Sharp pixels, 2× voxel resolution, anti-aliasing Off and 3D blur Off.
[Capture hashes and release identity](beta2-battle-captures.json).

The classic route and voxel-world images use the earlier candidate at
`03b48fc`, engine/data ABI `91b37e30`. The keyboard uses an earlier candidate
with the same naming overlay. These older images are distinct from the exact
released-APK captures. See [validation](../VALIDATION.md) for coverage.

## Earlier gallery

The gallery uses the combined landscape layout. The portrait image shows the
phone layout with on-screen controls. Some images are from earlier previews:
classic 2D scenes use alpha.3, and the shiny picker uses an alpha.5 candidate.
Older images include an FPS counter; current previews show it only when
**OPTION → SHOW FPS** is enabled.

| Image | Scene |
|---|---|
| [Classic town](classic-town.png) | Oldale Town and the Pokémon Center, with the map and touch menu. |
| [Classic route](classic-route.png) | Route 101 in the original 2D view. |
| [Voxel overworld](voxel-world.png) | Littleroot Town with optional voxel buildings and trees. |
| [Battle](battle.png) | Torchic encounters Poochyena, with touch battle commands. |
| [Party summary](party-summary.png) | Torchic’s stats, ability and moves. |
| [Phone layout](portrait-controls.png) | Portrait view with on-screen controls. |
| [Shiny odds](shiny-odds.png) | The five-choice picker, with original odds selected. |
| [Mystery events](mystery-events.png) | Seven optional actions with their availability. |
| [Aurora Ticket](mystery-deoxys.png) | The event description and individual activation dialog. |

Game artwork, device designs and trademarks belong to their respective owners;
the Android port’s MIT code licence does not relicense them. See
[credits and licensing](../../README.md#credits-and-licensing).
