# Releasing

A release is a ZIP with the builder, the engine-only 3DSX and the recipe. It
never contains a ROM, a data pack or anything extracted from the game.

## Prerequisites (maintainer machine)

- A tree from `tools/bootstrap.py --make` (or the maintainer's workspace).
- The supported ROM, and the ELF of the original game built from the same
  pinned upstream (`make` in the upstream tree's root produces
  `pokeemerald.elf`): the recipe generator needs its symbol table to know
  where each table lives in the ROM.
- PyInstaller (`pip install pyinstaller`).

## Steps

```
python tools/build_release.py --version 0.1.0 --rom baserom.gba \
    --gba-elf build/upstream/pokeemerald.elf \
    --make "make -j8 PYTHON=python"
```

This runs `make release`, writes the recipe, assembles
`dist/Emerald3DS-v0.1.0-Windows/` (builder, `payload/Emerald3DS.3dsx`,
`payload/Emerald3DS.smdh`, `payload/emerald3ds.recipe`, `payload/voxelgen/`,
`README.txt`, `LICENSES/`), zips it, audits the ZIP (including the ROM scan)
and writes `dist/SHA256SUMS.txt`.

## Before tagging

1. On a clean Windows machine without Python or devkitPro: extract the ZIP,
   run the builder with the ROM, install to an SD card, boot the game.
2. `emerald3ds-builder-cli verify --pak <SD>/3ds/emerald3ds/emerald3ds.pak`.
3. Update `CHANGELOG.md`, tag `vX.Y.Z`, attach the ZIP and `SHA256SUMS.txt`.

The same pack must never be attached to a release or an issue: it is
generated from the player's ROM.
