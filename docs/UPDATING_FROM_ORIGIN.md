# Taking new commits from origin

`origin/` is an unmodified copy of
[ZallaxDev/pokeemerald-3Ds-dualscreen](https://github.com/ZallaxDev/pokeemerald-3Ds-dualscreen),
kept as a squashed git subtree. Nothing in it is edited for Android, so a new
origin commit is taken by pulling it, not by porting it.

## The usual case

```sh
python3 tools/sync_origin.py --dry-run   # what would come in
python3 tools/sync_origin.py             # git subtree pull + origin.lock
python3 tools/bootstrap.py --make --apk  # rebuild everything
```

`sync_origin.py` refuses to run on a dirty tree, pulls `main` (or `--ref
<branch|tag|sha>`), rewrites `origin.lock`, and prints:

- the origin commits that came in;
- which origin **native units** changed (`3ds_port/src/*.c` built without the
  game headers, `voxel/ctr_voxel.c`) — the only files whose changes can need
  Android work;
- the result of `tools/check_shim_coverage.py`.

Origin changes fall into three groups:

| Origin change | What happens on Android |
|---|---|
| Game code, origin patches to pret/pokeemerald, the bottom-screen UI, voxel game modules, data generators, a new pinned pret commit | Nothing to do: `bootstrap.py` runs origin's own bootstrap and the Android build includes origin's `Makefile`/`full.mk`, so new sources, flags and generated data are picked up. |
| A native unit uses a libctru/Citro3D/Citro2D call it did not use before | `check_shim_coverage.py` names it. Implement it in `android/shim` (system) or `android/gpu` (graphics). |
| A new or changed PICA shader (`*.v.pica`) | Nothing to do: the fake `picasso` translates it to GLSL during the build. An opcode the translator does not know fails the build with its name and line. |

## When the build breaks

- **Compile error in a game unit**: clang is stricter than devkitARM's GCC in a
  few places. Prefer a flag in `android/toolchain/` (the compiler wrapper) or
  a forced-include header in `android/native/compat/`. Only if that is
  impossible, add a patch to `patches/android/` with a header saying why, and
  consider sending the fix upstream so the patch can be dropped.
- **Link error, undefined `Ctr*`/SDK symbol**: run
  `python3 tools/check_shim_coverage.py` for the list.
- **Origin changed its Makefile link step** (e.g. a new linker script input):
  the Android link rules in `android/native/Makefile` mirror origin's two
  links (image and release); update them to match.

## Checking the result

Build and run the debug APK on a device or the emulator (`docs/BUILDING.md`),
then compare against origin's release on a 3DS or Azahar: title screen, intro,
a battle, the bottom-screen menus, and the voxel overworld (OPTION → VOXEL 3D).
