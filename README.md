<h1 align="center">Pokémon Emerald Dual Screen for Android</h1>

<p align="center">
  An Android port of
  <a href="https://github.com/ZallaxDev/pokeemerald-3Ds-dualscreen">Pokémon Emerald 3Ds Dual Screen</a>
  by ZallaxDev: the game on the top screen, the touch interface on the bottom
  screen, and the optional voxel overworld — on your phone.
</p>

---

This is not a rewrite. The 3DS port ("origin") is included unmodified in
[`origin/`](origin/) and compiled as is — the decompilation, origin's patches,
its bottom-screen UI, its GPU compositor and its voxel renderer. Underneath it,
this repository re-implements the 3DS system libraries (libctru, Citro3D,
Citro2D) for Android on OpenGL ES 3, AAudio and app storage. Same code, same
pictures, same save file.

Because origin is never edited, its new commits are taken with one command
(see [Updating from origin](docs/UPDATING_FROM_ORIGIN.md)).

## Features

- **Made for the AYN Thor.** The top screen on the Thor's top display, the
  touch screen on its bottom display, the built-in controls as the 3DS's
  ([details](docs/AYN_THOR.md)).
- **Phones too: both screens on one display.** Portrait stacks them like the console;
  landscape puts them side by side, or the top screen large with the bottom
  screen beside it or on demand.
- **Touch screen.** The bottom screen is the touch interface origin designed
  (map, party, bag, Pokédex, PokéNav, save, options), driven by your finger.
- **Controls.** On-screen D-pad, circle pad and buttons; physical gamepads and
  keyboards, mapped by position like a Nintendo console by default.
- **Voxel overworld.** Origin's Citro3D renderer, running on OpenGL ES.
  Enable it in **OPTION → VOXEL 3D** on the bottom screen.
- **3DS saves.** The save is the same 128 KiB file the 3DS version writes
  (`/3ds/emerald3ds/emerald3ds.sav`); import or export it from Settings.

## Requirements

- Android 9 (API 28) or newer on a device that can run **32-bit ARM**
  (`armeabi-v7a`) apps. Origin's data pipeline uses 32-bit pointers, as the
  3DS does; phones that dropped 32-bit support cannot run it.
- OpenGL ES 3.0.

## Building

```sh
python3 tools/bootstrap.py --make --apk
```

This fetches pret/pokeemerald at the commit origin pins, applies origin's
patches, builds the decomp tools, compiles the whole tree for Android and
packages the APK. Requirements and details: [docs/BUILDING.md](docs/BUILDING.md).

**No game content is in this repository.** The build compiles the
decompilation's source tree on your machine; do not distribute the resulting
APK.

## Documentation

| Guide | What's inside |
|---|---|
| [Building](docs/BUILDING.md) | Requirements, build, install, testing on the emulator. |
| [Android architecture](docs/ANDROID_ARCHITECTURE.md) | How origin runs on Android unmodified. |
| [Updating from origin](docs/UPDATING_FROM_ORIGIN.md) | Taking new commits of the 3DS port. |
| [Origin's documentation](origin/docs/) | The game port itself: engine, assets, voxel overworld. |

## Credits and licences

- **Pokémon Emerald 3Ds Dual Screen** by ZallaxDev and contributors, MIT
  ([origin/LICENSE-PORT.md](origin/LICENSE-PORT.md)). Join their
  [Discord](https://discord.com/invite/tfqHF8496P) and support the original
  project on [Ko-fi](https://ko-fi.com/zallax).
- **pret/pokeemerald**, the decompilation, fetched at build time and not
  relicensed.
- **libctru, citro3d, citro2d** (devkitPro), zlib — their interfaces, and the
  parts noted in `THIRD_PARTY.md` files, are re-implemented or reused here.
- The Android port's own code is MIT ([LICENSE](LICENSE)). See
  [NOTICE.md](NOTICE.md).

This is an unofficial fan project, not affiliated with or endorsed by
Nintendo, Game Freak, Creatures or The Pokémon Company. Pokémon and Pokémon
Emerald are trademarks of their respective owners.
