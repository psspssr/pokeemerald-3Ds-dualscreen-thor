# AYN Thor setup and controls

The primary target is the [AYN Thor](https://www.ayntec.com/products/ayn-thor),
a clamshell Android handheld with two touch screens and built-in controls —
the closest thing to a 3DS that runs Android. Phones and tablets stay
supported with single-display layouts.

## Hardware

| | AYN Thor |
|---|---|
| SoC | Snapdragon 8 Gen 2 (Base, Pro, Max) or Snapdragon 865 (Lite) |
| 32-bit ARM | Required: confirm `armeabi-v7a` in `adb shell getprop ro.product.cpu.abilist` on the device |
| OS | Android 13 |
| Top screen | 6" AMOLED 1920x1080, 120 Hz, touch — Android's **primary** display |
| Bottom screen | 3.92" AMOLED 1240x1080, 60 Hz, touch — a **secondary** display |
| Controls | D-pad, two analog sticks, ABXY, L1/R1, L2/R2, Start/Select, Home/Back |

AYN lists the panel sizes, Android version and model chipsets on its
[Thor product page](https://www.ayntec.com/products/ayn-thor). CPU model alone
does not verify the firmware's 32-bit support or certify this game's performance.

## How the port uses it

Dual-display mode is the default when a second built-in display is present:

- **Top display (window 0)**: the activity's `SurfaceView`, showing the 3DS
  top screen (400x240) across the full **1920x1080** panel by default.
- **Bottom display (window 1)**: an Android `Presentation` on the second
  display, holding a `SurfaceView` for the 3DS bottom screen (320x240) and
  filling the full **1240x1080** panel. Touches map independently along each
  axis to 0..319 x 0..239 and become `KEY_TOUCH`, delivering touch input to
  origin's map, party, bag, Pokédex, PokéNav and options in that window.
- **Controls**: the built-in gamepad. On-screen controls are hidden by
  default in dual-display mode. If enabled in Settings with automatic
  visibility, use **Show controls / Hide controls** in the pause menu.
  Tapping outside the game picture also reveals them when using Fit mode.
- **Pause and settings**: press **L1 + R1 together** to open the Android
  pause menu. Choose Resume to continue, or Settings to change the layout and
  gameplay options. Individual L1/R1 presses still send the game's L/R inputs.
- **Refresh target**: both screens use one 59.83 Hz game clock and separate
  EGL surfaces. The renderer requests no per-window vsync wait, though the
  display driver can still block during presentation. The actual 120 Hz and
  60 Hz panel timing needs verification on Thor hardware.
- **Lifecycle**: switching apps, device sleep, the lock screen, or powering
  off the app's main display pauses gameplay. Reopening/waking resumes only
  when the app is active and unlocked, and any manual pause has been cleared.
  See [lid and sleep behavior](#lid-and-sleep). Turning off or disconnecting
  only the second display moves both screens to the main display.

## Lid and sleep

Gameplay pauses when Android reports sleep, a lock screen, or a main display
that is off or in a low-power state. Held controls are released, and a pending
shiny-escape question chooses Stay. Both game screens return after wake and
unlock. If you opened the pause menu before closing the lid, it stays paused
until you choose Resume.

This covers lid closure **when the firmware sleeps, locks, or powers off the
app's main display**. Android sleep/wake and locked-wake behavior were tested
with the real game in an emulator, including native counters that stop while
paused. **The Thor's physical lid sensor still needs a device test.** Firmware
that leaves Android awake on closure, or wakes it while the lid is still
closed, needs a verified lid signal; screen state alone cannot identify the
hinge position. Android's [lid policy permits sleep, lock, or no action](https://github.com/aosp-mirror/platform_frameworks_base/blob/android-13.0.0_r1/services/core/java/com/android/server/policy/PhoneWindowManager.java#L3534-L3550).

The app checks [device interactivity](https://developer.android.com/reference/android/os/PowerManager#isInteractive()),
[its display state](https://developer.android.com/reference/android/view/Display#getState())
and [whether keyguard is showing](https://developer.android.com/reference/android/app/KeyguardManager#isKeyguardLocked()).
It does not treat a secondary-panel disconnect as proof that the lid closed.

## Controls and shortcuts

The default face-button layout follows Nintendo positions. The Settings
**Face button mapping → By label** option follows the controller's reported
A/B/X/Y labels instead.

| Game input / action | Thor or gamepad | Keyboard |
|---|---|---|
| D-pad / movement | D-pad; left stick is the circle pad | Arrow keys |
| A — confirm | Right face button | X |
| B — back | Bottom face button | Z |
| X | Top face button | S |
| Y | Left face button | A |
| L / R | L1 / R1 individually | Q / W |
| Start | Start | Enter |
| Select | Select | Backspace or either Shift |
| App pause menu / Settings | **L1 + R1 together**; Android Back also works | Escape |
| Toggle fast-forward | R2 | Pause-menu action |
| Hold fast-forward | L2 | Tab |

Release the shortcut buttons before pressing them again. If you released a
button while another app had focus, one extra press-and-release may be needed
after returning.

Enable fast-forward controls and select **2×, 4× or 8×** in
**Settings → Gameplay → Quality of life** first. Audio is muted while speeding
up. [Fast-forward behavior and other options](QUALITY_OF_LIFE.md).

Touch the bottom display directly for game menus. To use optional on-screen
buttons, enable **On-screen controls in dual-display mode**; with Auto
visibility, choose **Show on-screen controls** from the pause menu. These
buttons are hidden by default when a second display is connected.

For a single-display device using landscape **Top screen only**, clicking
either stick switches between the top and bottom game screens. It does not
change the assignment of two physical displays.

## Game graphics and Summary menus

The app uses **Settings → Display → Image filtering → Sharp pixels** by
default. Upgrading an older preview switches its Linear
filter to Sharp once; choosing Smooth afterward is respected. Sharp also
keeps Pokémon sprites, names and health bars crisp in classic and voxel
battles. Changing this setting applies to an ongoing battle after resuming.
Voxel anti-aliasing remains separate. Fill still uses the entire panel.
For equally sized pixel blocks in the final display upscale, choose **Fit** and
enable **Integer scaling**; this adds borders.

**Settings → Display → Voxel resolution** offers:

| Setting | Internal voxel picture |
|---|---|
| 1× — Original | 400×240; lowest GPU cost |
| **2× — Sharp (default)** | **800×480** |
| 3× — High | 1200×720 |
| 4× — Ultra | 1600×960 |

This draws finer 3D geometry while retaining the original pixel art. Existing
resolution choices are preserved. Higher levels use more GPU time and memory;
choose 1× if a scene runs slowly. Unsupported allocations fall back to a lower
level, but this does not automatically detect a low frame rate. Settings shows
when a saved request cannot be used on the current device.

**Settings → Display → Voxel anti-aliasing** offers Off, 2× and 4× on supported
GPUs. It starts off, smooths voxel scenery before sprites and menus are drawn,
and leaves classic 2D rendering alone. Higher settings can reduce performance.
Unsupported levels are omitted; a saved setting from another device falls
back to a supported lower level. This is separate from the game's voxel blur
effect.

For a fully sharp voxel view, turn **OPTION → 3D BLUR** off in the game.
New configurations default to Off; a previously saved choice is respected.

Enable the optional overworld through **OPTION → VOXEL 3D** in the game.
For voxel battles, enable VOXEL 3D, scroll the same options list and turn
on 3D BATTLE. Both start off. This draws voxel battle scenery behind the
original 2D Pokémon, health bars, text and move animations, where the voxel
world is available. **BATTLE SCENE** remains the original animation setting.
These game options are separate from Android's Settings menu.

The FPS counter is optional: **OPTION → SHOW FPS**, off by default.

Native Party/Summary menus use the bottom screen. In Summary, tap a move to
preview it and tap the selected row again
to confirm. While choosing where to move it, tap another row to preview the
position and again to swap, or tap the fifth-row **Cancel** to abandon the
reorder. When learning a new move, that fifth row is the proposed move;
use the header Cancel or B to leave without replacing a move.

## Screen scaling

Dual-display **Fill** also expands the original GBA menu area.
Full-screen Summary and PC views use the whole bottom panel;
Party, Bag and Pokédex views use the full height beside their navigation
column. Touch targets expand with the visible content. Map, battle commands
and other menus that already fill their area keep their layout. Fit and
single-display layouts retain the original framing.

**Settings → Dual display → Screen scaling** offers two choices:

| Choice | Top panel picture | Bottom panel picture |
|---|---|---|
| **Fill both displays (default)** | 1920x1080 | 1240x1080 |
| **Fit original proportions** | 1800x1080, centred | 1240x930, centred |
| Fit with integer scaling enabled | 1600x960, centred | 960x720, centred |

Fill shows the complete image without cropping. It stretches the image to
match each panel's proportions; Fit preserves the original 3DS proportions
with borders. Integer scaling applies to Fit and single-display phone layouts.
Both windows hide Android's system bars, which remain available by swiping.
Screen swapping, reconnecting a display and returning from Settings retain
the chosen scaling mode. Single-display phone layouts keep their existing
scaling and control placement.

## Other dual-display handhelds

Some devices report their displays in the opposite order. Use
**Settings → Dual display → Top screen on** to choose the main or second
display for the top game screen.

## Testing

The local app instrumentation run uses a 1920×1080 main display and a
1240×1080 presentation display. CI uses a smaller 720×1280 main display to
reduce software-rendering load, with the same 1240×1080 second display.
The tests check full panel coverage, corner touch coordinates, both display
assignments, Fit/Fill switching, rotation, resizing and removal.
The production GLES presenter also has source-corner and outer-edge pixel checks
at both panel sizes, so filling the panels cannot silently crop the image.
App instrumentation uses the separate display-test renderer on an x86_64
emulator; the standalone GLES checks exercise the production presenter.
Follow [BUILDING.md](BUILDING.md) for these tests and the real ARM game build.
Physical lid behavior, panel timing, thermal performance and sustained game
frame rate require testing on Thor hardware.
