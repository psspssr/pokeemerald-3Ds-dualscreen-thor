# Notices

## origin/

[ZallaxDev/pokeemerald-3Ds-dualscreen](https://github.com/ZallaxDev/pokeemerald-3Ds-dualscreen),
included unmodified as a git subtree. Its port code is MIT
(`origin/LICENSE-PORT.md`); its other components keep the terms listed in
`origin/NOTICE.md`, including pret/pokeemerald (no stated licence, fetched at
build time and never vendored) and devkitARM's `3dsx.ld` (MPL 2.0).

## Android port

- `android/native/android.ld.in` is derived from origin's
  `3ds_port/emerald3ds.ld.in` (itself a modified devkitARM `3dsx.ld`) and from
  GNU ld's default shared-object script; the parts taken from `3dsx.ld` stay
  under the Mozilla Public License 2.0 (https://mozilla.org/MPL/2.0/).
- `android/shim` and `android/gpu` re-implement the interfaces of
  **libctru**, **citro3d** and **citro2d** (devkitPro, zlib licence). Code
  reused from them keeps its licence header; each directory's
  `THIRD_PARTY.md` lists it.

## Game content

Pokémon Emerald's code, data, graphics, audio and text belong to Nintendo,
Game Freak, Creatures and their respective rights holders. The Android port's
MIT licence does not grant rights to that content. Source patches contain
context from the files they modify, and screenshots depict game artwork.

Current private preview APKs include generated game data. Making the source
repository public would also expose its historical release assets; the private
release workflow is not a public game-data distribution workflow. See
[release packaging](docs/RELEASING.md).
