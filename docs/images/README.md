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

## Upstream 0.5.0 gameplay captures

The current gallery shows the upstream 0.5.0 integration at commit `43b17d6`,
with the classic sprite and soft-reset fixes. Captures come from the local
ARM-translation Android emulator at 1920×1080 and 1240×1080, with no
secondary-preview thumbnail. The hero device is an illustrative mockup using
the new native map and dock, not a physical Thor photograph.

| Image | Scene |
|---|---|
| [Classic route](classic-route-beta.png) | Route 101 with the original 2D renderer and correct player/NPC sprites. |
| [2× Sharp](voxel-sharp-2x.png) | The same area with voxel scenery at 800×480 internal resolution and blur off. |
| [Voxel battle](voxel-battle-beta.png) | A natural Poochyena encounter with voxel scenery and Sharp battle sprites. |
| [Battle touch screen](thor-battle-touch.png) | Torchic's move selector in the new native bottom interface. |
| [4× Ultra](voxel-ultra-4x.png) | Earlier Route 102 at 1600×960 internal resolution. |
| [Touch keyboard](thor-touch-keyboard.png) | Protagonist name entered by finger on the lower panel. |

The [beta.2 battle images](beta2-battle-captures.json) are preserved as
`voxel-battle-beta2.png` and `thor-battle-touch-beta2.png`, with their original
hashes and release identity. The keyboard and 4× images also remain earlier
candidates. See [validation](../VALIDATION.md) for current and historical coverage.

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
