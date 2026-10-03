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
  axis to 0..319 x 0..239 and become `KEY_TOUCH`, so origin's
  touch interface (map, party, bag, Pokédex, PokéNav, options) works by finger
  on the real bottom screen, as on the console.
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
- **Lifecycle**: Android's activity pause callback pauses the game when
  switching apps. The same applies when lid closure triggers that callback;
  the firmware's lid behavior needs a device check. The Presentation is
  dismissed and recreated with the activity. If the second
  display disappears, both screens move to the top display (a single-display
  layout) until it returns.

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

Enable fast-forward controls and select **2× or 4×** in
**Settings → Gameplay → Quality of life** first. Audio is muted while speeding
up. [Fast-forward behavior and other options](QUALITY_OF_LIFE.md).

Touch the bottom display directly for game menus. To use optional on-screen
buttons, enable **On-screen controls in dual-display mode**; with Auto
visibility, choose **Show on-screen controls** from the pause menu. These
buttons are hidden by default when a second display is connected.

For a single-display device using landscape **Top screen only**, clicking
either stick switches between the top and bottom game screens. It does not
change the assignment of two physical displays.

## Screen scaling

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
They use the separate display-test renderer on an x86_64 emulator. Follow
[BUILDING.md](BUILDING.md) for these tests and the real ARM game build.
Physical lid behavior, panel timing, thermal performance and sustained game
frame rate require testing on Thor hardware.
