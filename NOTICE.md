# Notices

## origin/

[ZallaxDev/pokeemerald-3Ds-dualscreen](https://github.com/ZallaxDev/pokeemerald-3Ds-dualscreen),
included unmodified as a git subtree at `bcae4b70dac6` (0.5.0). Since 0.5.0,
its original port code, tooling and documentation are licensed under
**Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International**
(`origin/LICENSE-PORT.md`), within the scope stated there. Versions through
0.4.x remain MIT; third-party material, including the voxel renderer's MIT
contributions, keeps its own terms. See `origin/NOTICE.md` for other components,
including pret/pokeemerald (no stated licence, fetched at build time and never
vendored) and devkitARM's `3dsx.ld` (MPL 2.0).

Android overlays identify their modifications to this upstream material and
retain the applicable CC BY-NC-SA 4.0 terms. This does not relicense the
independent Android implementation code, which remains MIT under `LICENSE`.

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
