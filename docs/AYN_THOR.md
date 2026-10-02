# Target device: AYN Thor

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
  top screen (400x240) scaled to fit: x4.5 fills the height at 1800x1080,
  integer x4 is 1600x960.
- **Bottom display (window 1)**: an Android `Presentation` on the second
  display, holding a `SurfaceView` for the 3DS bottom screen (320x240) and
  receiving its touches: x3.875 fills the width at 1240x930, integer x3 is
  960x720. Touches map to 0..319 x 0..239 and become `KEY_TOUCH`, so origin's
  touch interface (map, party, bag, Pokédex, PokéNav, options) works by finger
  on the real bottom screen, as on the console.
- **Controls**: the built-in gamepad. On-screen controls are hidden by
  default in dual-display mode and come back when the screen is touched
  outside the game picture, if enabled in Settings.
- **Mapping**: by position like a Nintendo console (Thor's bottom face button
  is 3DS B, right is A, left is Y, top is X); left stick is the circle pad;
  L1/R1 are L/R. With optional fast-forward controls enabled, R2 toggles the
  selected 2×/4× rate and L2 holds it. A label-based mapping is in Settings for
  controllers set to the Xbox layout.
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

## Other dual-display handhelds

Some devices report the bottom screen as the primary display and the top as
the secondary one (AYANEO Pocket DS, Retroid's dual-screen add-on). Settings
→ Display has "Top screen on: main display / second display" for them.

## Testing

The app instrumentation tests create a Thor-sized 1240×1080 presentation
display and exercise the bottom window, touch mapping and display removal.
They use the separate display-test renderer on an x86_64 emulator. Follow
[BUILDING.md](BUILDING.md) for these tests and the real ARM game build.
Physical lid behavior, panel timing, thermal performance and sustained game
frame rate require testing on Thor hardware.
