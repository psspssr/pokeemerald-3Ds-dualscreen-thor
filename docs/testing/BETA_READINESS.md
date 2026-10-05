# Thor beta readiness

This pass targets menu correctness, clearer voxel rendering and a stable Android
game session. A beta candidate must pass the checks below; emulator results do
not establish physical Thor performance or complete-game coverage.

The menu/graphics implementation and bounded emulator pass are complete.
They include the final Hoenn Pokédex touch correction and repeated graphics-pool
checks. [Validation](../VALIDATION.md) records the tested candidates, remaining
late-game routes and hardware limits. The exact beta.1 production APK also
passed the final emulator/save-export check, and the downloaded release bytes
match that tested installation.

The beta.2 follow-up corrects battle sprite and text filtering. Its release
workflow and 668 GLES assertions passed, followed by a gameplay check of the
exact downloaded APK. [Validation](../VALIDATION.md#previous-beta-010-beta2)
records the release identity, broader candidate tests and coverage limits.

The beta.3 polish pass fixes controller releases inside Settings dialogs, PC
initialization/cleanup failures and recovery of unfinished Pokémon or item
moves. It passed 136 app tests, native sanitizer/conservation checks and
ordinary plus deliberately failed PC reopening in the ARM emulator.
[Validation](../VALIDATION.md#previous-beta-010-beta3) separates the published
release from the unpublished build used to trigger those failure paths.

Beta.4 groups Settings into five sections and adds consistent controller,
stick and keyboard navigation. Voxel work now uses spare time before frame
pacing; NPCs reuse newly freed slots in the same camera update. A rapid
pause/resume acknowledgement race was also fixed. All 154 app tests and the
release workflow passed; [validation](../VALIDATION.md#latest-beta-010-beta4)
records the exact APK check and emulator performance limits.

## Work order

1. **Record the baseline.** Preserve normal saves and settings, then inspect
   alpha.8 on separate 1920×1080 and 1240×1080 surfaces. Record actual touch,
   controller and back/cancel behavior, not just screenshots.
2. **Review the upstream update.** Rehearse the exact `c330c0a1aece` import,
   including voxel terrain, fog, sprite-occlusion and frame-work improvements.
   Keep `origin/` unchanged from its pin, rebase only necessary Android overlays,
   and verify a complete ARM build before adopting it.
3. **Fix menu routes.** Exercise the families below, with full bottom-panel
   content and matching hit targets. Check each transition, selected-row state,
   scrolling, quantity/confirmation dialog and cancellation path. Retain the
   supported phone/Fit fallback.
4. **Improve voxel detail.** Evaluate higher internal rendering resolution,
   keeping original 2D pixels and menus sharp. Detail must survive the final
   display transfer. Verify depth, CPU/GPU composition, MSAA, resource reuse,
   capability/allocation fallback and pause/display recreation together.
5. **Integrate and play.** Rebuild one recorded candidate, exercise its actual
   ARM game and compare the same scenes before/after. Validate normal saves and
   exports after gameplay and menu mutations.
6. **Independent bug audit.** Review the integrated changes and repeat risky
   routes, storage/lifecycle failure paths and relevant regressions. Resolve
   reproduced blockers before promoting or publishing the candidate.

## Menu coverage

| Family | Routes to exercise |
|---|---|
| Field navigation | Map, Party, Bag, Trainer Card, Pokédex, PokéNav, Save, Options |
| Party and Summary | Selection, actions, all pages, move details/reorder, learning replacement/cancel, HM restrictions |
| Bag | Every pocket, item use, held items, quantity/confirmation, TM/HM selection, battle Bag, registered items |
| PC | Deposit/withdraw/move, boxes and Party drawer, Summary, item storage, cancellation |
| Pokédex | List, details, pages, search/filter and return routes available in the fixture |
| PokéNav | Map, condition/search, Match Call and ribbons where unlocked |
| Battles and progression | Commands, targets, Party switching, Bag, stats/level-up, evolution, capture/name entry, starter choice |
| Other game menus | Shop/buy/sell, Pokéblocks/contests, link entry points and any reachable unsupported-feature exits |
| Later facilities and services | Battle Frontier selection/swap menus, field-service choices, Mirage Tower/fossil scenes and their return paths; record host-only versus live coverage |
| Android app | Pause/Resume, Settings, display/graphics selectors, controls, QoL, mystery events, import/export/backups, diagnostics |

Late-game saves used to reach specific routes must be labelled as QA fixtures.
Unreachable or unsupported routes are recorded separately from passing tests;
an inventory entry does not imply that a full adventure was played.

## Regression requirements

- Preserve raw Emerald/GBA save compatibility and original files during QA.
- Keep optional gameplay changes disabled initially and Sharp filtering default.
- Maintain L1+R1 pause, L2 hold, R2 toggle and 2×/4×/8× speed selection.
- Correctly pause for Home, lock/sleep and main-display loss; keep an existing
  manual pause after wake. Reconnect/swap/fallback must retain valid touch input.
- Check mixed CPU/GPU drawing, classic/voxel transitions and cached allocation
  behavior at every supported quality level. A quality setting is not a promise
  of a particular frame rate.
- Run the relevant host/native sanitizers, app tests, GLES pixel assertions,
  strict overlay/upstream checks, full ARM link and release packaging/lint.

## External checks before calling hardware support complete

The physical Thor still needs a repeatable test of both panels, lid sensing,
audible output, sustained frame pacing, memory, thermals and battery behavior.
Use the [hardware checklist](HARDWARE_TEST.md). Broader later-game progression
also remains necessary; avoid claiming that bounded playtests prove every game
scene correct. Final observed coverage belongs in [Validation](../VALIDATION.md).
