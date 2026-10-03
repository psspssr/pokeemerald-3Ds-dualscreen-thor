# Mystery events

Open **Settings → Gameplay → Mystery events** while standing still in the
ordinary overworld. Each entry explains what it unlocks and shows its current
status. Choose an available entry, read its details and tap **Activate**.
Nothing is activated automatically.

Start or Continue an adventure, receive the Pokédex and make a normal in-game
save first. Finish conversations, close in-game menus and stop moving before
opening the Android settings. After activating an event, **save normally in
the game** to keep the change.

Leave a Battle Frontier challenge before activating an event. Some challenge
rooms temporarily replace your party; events are blocked there so a gift cannot
be discarded when your normal party returns. Ordinary facility lobbies remain
available after their dialogue has finished.

## Available options

| Option | Result |
|---|---|
| **Eon Ticket** | Enables Southern Island, where Emerald offers the opposite Latias/Latios encounter and Soul Dew. |
| **Mystic Ticket** | Enables Navel Rock for Lugia and Ho-Oh. Each encounter keeps its own progress. |
| **Aurora Ticket** | Enables Birth Island and the original Deoxys triangle puzzle/encounter. |
| **Old Sea Map** | Enables Faraway Island and the original Mew hide-and-seek encounter. |
| **Jirachi gift** | Adds a level-5 Jirachi to an empty party slot, with the player's OT and normal moves. |
| **Celebi gift** | Adds a level-5 Celebi to an empty party slot, with the player's OT and normal moves. |
| **Regi Doll set** | Restores missing Regirock, Regice and Registeel dolls to decoration storage. |

The four island options use Emerald's existing tickets, access flags, ferry
routes and encounters. **Ferry travel still requires becoming Champion**;
receiving a ticket does not advance the story. Use the Lilycove ferry when it
becomes available. If a ticket is in PC item storage, withdraw it first.

Jirachi and Celebi are explicitly **offline gifts**, not replicas of a
historical distribution or new Emerald quests. The original game creates and
encrypts them normally. The wild shiny-odds option does not affect these gifts.
The Regi dolls are an offline restoration option for the supported decorations.
The menu does not install unused debug Wonder Cards or arbitrary external
Mystery Gift payloads.

## Existing progress and storage

- An already unlocked island does not grant a duplicate ticket. Partial access
  can be repaired by adding only a missing ticket or access flag.
- Caught or currently defeated island encounters are preserved. The menu does
  not reset puzzles, encounters or progress. Emerald's normal Hall of Fame
  rematch behavior remains intact for defeated encounters.
- A Jirachi/Celebi already recorded as caught, or present in the party or PC,
  blocks another gift. Trading or releasing a Pokémon does not erase its
  ordinary caught record.
- Gifts need an empty party slot. Full Bag, party or decoration storage is
  reported before changing the reward or flags.
- Owned or placed Regi dolls are never duplicated. Missing dolls can be restored
  again after deletion or trading; there is no separate permanent claim flag.

## Saves and upstream updates

Tickets, event flags, Pokémon, Pokédex entries and decorations use the normal
GBA save fields. No header, save expansion or custom unused claim bits are
introduced. These changes travel with the same raw 128 KiB `.sav` used by the
original game and compatible emulators.

The Android host services a selected action on the paused game thread, without
advancing gameplay or writing a save. Native code rechecks the current game
state and capacity immediately before applying it. Leaving a screen before a
queued UI task starts cancels that task; a started native action finishes once.

The imported `origin/` remains unchanged. The small
[session-lifecycle overlay](../patches/android/030-mystery-events.patch) and
[native event adapter](../android/native/src/mystery_events.c) live outside it.
See [upstream updates](UPDATING_FROM_ORIGIN.md) for rebuilding and validating a
new pin.
