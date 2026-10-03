# Optional quality-of-life settings

Open the Android pause menu, then **Settings → Gameplay → Quality of life**.
Every feature starts **off**. Preferences are stored by Android, separately
from the original Emerald `.sav` format.

## Fast-forward

Enable **fast-forward controls**, then choose **2× or 4×**. The selected rate
starts at 4×, but play remains at normal speed until activated:

- **R2:** toggle fast-forward on or off.
- **L2:** hold to fast-forward; release to return to normal unless R2's toggle
  is still on.
- **Keyboard Tab:** hold to fast-forward.
- **Pause menu:** start or stop fast-forward without a controller.

The toggle is runtime state and starts off on a new app launch. Held input is
released when the app pauses, opens a menu or loses its controller. A trigger
already held across a pause does not count as another toggle press.

Game logic advances multiple times per displayed frame; the renderer keeps its
59.83 Hz target rather than asking the displays to draw 240 frames per second.
Actual acceleration depends on the device and scene. Audio is deliberately
muted during acceleration, with queued sound discarded so returning to 1× does
not play a delayed backlog.

## Shiny odds

The selector offers normal odds and boosts of up to **4×, 16× or 64×**.
The displayed odds are approximate targets. Only newly generated ordinary wild
encounters are affected. Existing Pokémon, starters, gifts, trainers, roamers
and fixed/scripted creation retain their original behavior.

The boost creates a genuine Gen III shiny personality before the Pokémon is
encrypted. It preserves nature, gender, ability parity and species-specific
Unown/Wurmple constraints. If those constraints cannot be satisfied together,
the original personality is retained. Natural shinies are never removed, and
no extra random-number draws are introduced. A boosted shiny stays shiny when
its save is opened in the original GBA game or a compatible emulator.

## Shared party experience

Existing participant and held Exp. Share rewards stay unchanged. Additional
party members that did not battle and are not already earning Exp. Share
rewards receive half the defeated Pokémon's base EXP, with a minimum of one.
The original trainer/trade/Lucky Egg modifiers and level-up, move-learning and
EV paths still apply.

Empty slots, eggs, fainted Pokémon, level-100 Pokémon and a story partner's
Pokémon are excluded. The policy is read once per defeated opponent, so changing
Settings during a reward sequence does not split that sequence between rules.

## Save backups

Enable **Keep save backups** to retain the last five completed manual in-game
saves, including the first save that overwrites an older game. This is not an
autosave or an emulator save state.

The port verifies the freshly written slot's signatures, sections, counter and
checksums before copying the complete 128 KiB image. A new snapshot is written
to a temporary file, synced and renamed before older snapshots are rotated.
If creating the new snapshot fails, previous snapshots are kept; the normal
save remains valid. Disabling the option leaves existing snapshots available.

**Restore a save backup** lists snapshots newest first. Restoration is staged,
then applied on restart using the same path as save import; the previous active
save is retained as `emerald3ds.sav.bak`. Backups live in
`sdmc/3ds/emerald3ds/backups/` and are standard `.sav` files.

## Shiny escape confirmation

When enabled, deliberately escaping from a living wild shiny asks for
confirmation. **Stay** is the default. Closing the dialog, backgrounding the
app, or losing the UI cancels the escape.

The guard covers Run/Safari Run before guaranteed-run checks, escape items
before consumption, and directly selected escape moves before committing the
turn. A randomly called escape move, such as one selected by Metronome, is
checked before escape; declining that already-started move makes it fail
normally, so its turn and PP remain spent. Opponent-driven fleeing is unchanged.

It also covers answering **No** to “Use next Pokémon?” after your active
Pokémon faints. Choosing **Stay** then opens the required replacement choice.
A normal loss when the whole party has fainted is unchanged.

## Upstream and compatibility

`origin/` remains an exact upstream import. Small, tracked overlays in
`patches/android/` are applied only to the generated build tree, with strict
context and source checks. Optional hooks preserve the original path when
features are off. The graphics overlay also removes the location-name banner's
unintended wrap onto the bottom of the taller field viewport.

The current upstream implements a voxel **overworld**, not voxel battles.
Its normal **BATTLE SCENE** setting controls the original battle animations.
