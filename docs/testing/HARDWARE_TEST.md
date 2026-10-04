# First Thor test and performance capture

Use the published alpha.8 APK as the reference build while development moves
on `dev`. CI runs on pushes to both `dev` and `main`, and on pull requests.
Development pushes do not publish APK releases.

## Short device check

Record the Thor model, firmware, app version and selected power mode. Use the
same saved location when comparing builds. Check:

1. Continue, movement, A/B, individual L1/R1, and L1+R1 pause/Resume.
2. Both panels fill; Map/Party/Bag touch reaches the intended control.
3. Close the lid during gameplay, wait, reopen, and verify that the game did
   not advance. Repeat after opening the app pause menu: it must remain paused.
4. Home and return, lock/unlock, and the single-display fallback.
5. Normal speed, optional 2×/4×/8×, R2 toggle and L2 hold; listen for audio glitches
   at normal speed after returning from fast-forward and sleep.
6. Save normally, quit, relaunch and Continue. Export a copy before testing
   imports or restoring backups.

For a longer pass, compare the same town, battle and voxel scene after 15–30
minutes. Record visible stutter, audio breaks, heat and power-mode changes.
An emulator run does not establish physical panel timing, thermals or battery
consumption. The [lid signal boundary](../AYN_THOR.md#lid-and-sleep) still applies.

## Optional diagnostics in development builds

Open **Settings → About → Export diagnostics** to save a JSON report through
Android's document picker. Choose a new document in Downloads. The report
includes app/device/display information, the renderer and a memory snapshot.
It excludes saves, game data, document paths and persistent device identifiers.

For a reproducible issue, enable **Record diagnostic history and timings** in
About, return to the game, reproduce the issue, then export. Recording starts
off and keeps only bounded recent events and presentation samples in memory.
Turning it off clears that history. Nothing is uploaded automatically.

The report distinguishes the current window assignment from the last recorded
active assignment, since opening Settings pauses the game and removes its
secondary window. Timings describe presentation calls, not GPU execution or
physical panel scanout. This option is included in alpha.8; alpha.7 predates it.

## Developer trace

With USB debugging enabled and the game already running, select the exact
device shown by `adb devices`:

```sh
python3 tools/capture_android_profile.py --serial DEVICE_SERIAL \
  --seconds 30 --out build/profiles/thor-town-1x
```

Play the selected scene during capture. The helper requires Android 10+,
records a bounded Perfetto trace, and collects process memory, display and
thermal snapshots. It leaves game data and device performance settings alone.
Each run requires a new output directory. The trace includes system scheduling
and process names; sharing is manual.

Open `game.perfetto-trace` in the [Perfetto viewer](https://ui.perfetto.dev/).
Look at the game's threads, CPU scheduling/frequency and memory over time;
compare identical scenes/settings between builds. The helper records the PID
and rejects a capture if the game process changes.

Native game rendering uses SurfaceView. Android UI `gfxinfo` FPS or
Choreographer timing is not a measurement of the game's two display surfaces.
The optional Android 12+ FrameTimeline source is useful system context, but
Perfetto documents a [SurfaceView coverage limitation](https://perfetto.dev/docs/data-sources/frametimeline).
Game presentation intervals also differ from simulation speed at 2×/4×/8× and
from the panel's actual scanout time. Keep those measurements separate.

References: [Perfetto system tracing](https://perfetto.dev/docs/getting-started/system-tracing)
and [Android game profiling tools](https://developer.android.com/games/tools).
