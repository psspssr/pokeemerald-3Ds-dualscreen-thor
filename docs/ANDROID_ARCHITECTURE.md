# Android port architecture

This repository ports [Pokémon Emerald 3Ds Dual Screen](https://github.com/ZallaxDev/pokeemerald-3Ds-dualscreen)
("origin") to Android **without forking it**. Every source file of origin —
the pret/pokeemerald decompilation with origin's patches, origin's game-side
bridge, *and origin's 3DS native backend* (`3ds_video.c`, `ctr_voxel.c`,
`3ds_audio.c`, ...) — is compiled unchanged. What changes is the platform
underneath it: libctru, Citro3D and Citro2D are re-implemented for Android on
OpenGL ES 3.0, AAudio, pthreads and app storage.

This preserves upstream's game logic, bottom-screen UI and save format while
keeping platform changes outside its tree. Rendering parity still requires
runtime comparison. `tools/sync_origin.py` imports new versions into `origin/`
and records their exact commit and tree; the build then picks them up.

```
 origin/ (git subtree, never edited)          android/ (this port)
 ┌─────────────────────────────────────┐      ┌──────────────────────────────────┐
 │ upstream.lock → pret/pokeemerald     │      │ toolchain/  fake devkitPro: the  │
 │ patches/pokeemerald/*.patch          │      │   compiler, binutils and picasso │
 │ 3ds_port/  Makefile, full.mk         │─────▶│   names origin's Makefile calls  │
 │   src/*.c (game + native units)      │      │ native/Makefile  includes origin │
 │   src/voxel/*.c, voxel.v.pica        │      │   Makefile, swaps the link       │
 │ tools/bootstrap.py                   │      │ shim/  libctru on Android        │
 └─────────────────────────────────────┘      │ gpu/   Citro3D/Citro2D on GLES 3  │
                                               │ host/  JNI bridge (ctr_host.h)   │
                                               │ app/   Gradle app, UI, controls  │
                                               └──────────────────────────────────┘
```

## Target

- ABI: **armeabi-v7a only**. Origin's data pipeline is 32-bit by design (asset
  stubs are looked up by `(u32)ptr`, script bytecode and MP2K songs carry
  32-bit pointers relocated by `3ds_script_loader.c`). A 32-bit ARM build has
  the same pointer size, alignment and endianness as the 3DS's ARM11. The
  native link and loader preserve the addresses used by generated data.
  64-bit-only devices cannot run the game build.
- `minSdkVersion 28` (fopencookie in bionic, AAudio), `targetSdkVersion 35`.
- GLES 3.0.
- Primary device: **AYN Thor** (dual display, see `docs/AYN_THOR.md`): the
  3DS top screen on the top display, the bottom screen on the bottom display
  (window 1, an Android `Presentation`). Single-display phones stay supported.

## Build flow

1. `tools/bootstrap.py` runs **origin's own** `origin/tools/bootstrap.py
   --dir build/upstream`: pinned pret/pokeemerald + origin's patches + origin's
   `3ds_port/`, `tools/`, `builder/` overlaid.
2. It applies `patches/android/*.patch` (an empty series is the goal; any patch
   here is a bug report waiting to be sent upstream).
3. It copies `android/` to `build/upstream/android/` and builds the decomp
   tools (`make tools generated`).
4. `make -C build/upstream/3ds_port -f ../android/native/Makefile` includes origin's
   `3ds_port/Makefile` (hence `full.mk`) with `DEVKITPRO`/`DEVKITARM` pointing
   at `android/toolchain/` (generated wrappers):
   - `arm-none-eabi-gcc` → NDK clang `--target=armv7a-linux-androideabi28`,
     dropping 3DS-only flags (`-march=armv6k`, `-mtune=mpcore`,
     `-mfloat-abi=hard`, `-mtp=soft`, `-specs=...`), adding `-fPIC` and the
     shim include path.
   - `arm-none-eabi-as/cpp/nm/readelf/objcopy` → GNU binutils for
     `arm-linux-gnueabi` (data-only objects, GNU syntax, maps origin's scripts
     parse).
   - `picasso` → `tools/picasso2glsl.py`: a `.v.pica` becomes a `.shbin`
     container holding GLSL ES 3.00 and the uniform/output tables;
     `DVLB_ParseFile` reads it.
   - Only the link rules are overridden: the executable becomes
     `libemerald.so`, linked with GNU ld against the NDK sysroot using
     `android/native/android.ld.in` (origin's `emerald3ds.ld.in` adapted to a
     shared object, keeping `.gamedata`).
   - RomFS outputs (`romfs/**`) are produced by origin's rules and packaged
     as APK assets under `romfs/`.
5. Gradle (`android/app`) packages `libemerald.so` and its fixed-address loader
   `libemeraldboot.so` into `jniLibs/armeabi-v7a`
   and the RomFS into `assets/romfs/`.

The independent Gradle `harness` build type compiles `android/host/test/fake_game.c`
for ARM and x86_64 and uses package ID `com.emerald3ds.android.harness`. It exercises
the app/host contract without the full game or its data. Debug and release game
builds require the real native outputs; test results identify which variant ran.

## Runtime

- Java/Kotlin `GameActivity` (not NativeActivity) hosts a `SurfaceView` and
  an overlay `View` with touch controls. JNI (`android/host/src/`) fills the
  shared host state declared in `android/host/include/ctr_host.h`.
- The game runs on its own native thread calling origin's `main()`
  (`main_3ds.c`). The game owns its loop exactly as on the console; the shim
  blocks in `aptMainLoop()` while the activity is paused.
- Paths: `romfs:/x` → `<filesDir>/romfs/x` (assets extracted on first run or
  update); `sdmc:/x` → `<externalFilesDir>/sdmc/x`, so saves live at
  `.../sdmc/3ds/emerald3ds/emerald3ds.sav`, the same file as on a 3DS.
  Redirection is done with GNU ld `--wrap` on libc file functions, so no source
  file changes.
- Screens: the top screen (400x240) and bottom screen (320x240) are kept as GL
  textures and drawn into rectangles the app chooses (portrait: stacked like
  the console; landscape: side by side or large top). Touches on the bottom
  rectangle become `KEY_TOUCH` + `hidTouchRead`.
- Engine-only releases require a pack generated for that Android executable's
  ABI. Upstream 3DS packs have different executable pointer values. Save files
  retain their original format and are independent of the data-pack ABI.

## Component ownership

| Directory | Contents |
|---|---|
| `android/host/include/ctr_host.h` | The only contract between the app/JNI side and the native shim. |
| `android/shim/` | libctru: `<3ds.h>` and `3ds/*.h` (minus `3ds/gpu/`), svc/os/threads/sync, APT, HID, romfs/sdmc path mapping, NDSP on AAudio, console, linear/VRAM heaps, `--wrap` file functions, `exit`. |
| `android/gpu/` | `<citro3d.h>`, `<citro2d.h>`, `<tex3ds.h>`, `3ds/gpu/*.h`, GSP/GX/gfx (framebuffers, transfers, `gspWaitForVBlank`), EGL, presentation, frame pacing, PICA shader runtime. |
| `android/toolchain/`, `android/native/`, `tools/` | Toolchain wrappers, build, bootstrap, origin sync, shim coverage check. |
| `android/app/` | Gradle project, activity, controls, settings, file import/export. |

## GPU emulation rules (android/gpu)

The backend code relies on 3DS memory semantics, so the emulation models them:

- **Memory.** `linearAlloc`/`vramAlloc` return ordinary CPU memory, recorded in
  a region table (`ctrshim_mem.h`) so a pointer can be classified (linear,
  VRAM, framebuffer, texture data, render-target buffer).
- **Textures** (`C3D_Tex`) keep their CPU `data` in PICA tiled layout (8x8
  Morton tiles, PICA byte order) exactly as on hardware, and a GL texture.
  Before a draw samples one that is not a render target, changed tile rows are
  found by comparing against a shadow copy, detiled, converted and uploaded.
  `C3D_SyncTextureCopy`/`GX_TextureCopy`/`C3D_TexFlush`/`GSPGPU_FlushDataCache`
  mark ranges dirty explicitly.
- **Render targets** are FBOs; a texture that is a render target is
  GPU-authoritative. Screen targets keep the 3DS's rotated geometry
  (240x400 / 240x320) so origin's projection matrices are right unmodified;
  presentation rotates.
- **Vertex data** is read from client memory at draw time
  (`AttrInfo`/`BufInfo` → streamed GL buffer), so CPU writes and
  `C3D_SyncTextureCopy` into "VRAM" vertex buffers need no tracking.
- **TexEnv** stages, alpha test, blending, depth, logic op, write mask,
  scissor and cull are turned into GL state plus a generated fragment shader
  cached by TexEnv configuration (as Citra/Azahar do).
- **Framebuffers** (`gfxGetFramebuffer`) are CPU memory in the LCD's column
  layout. CPU writes reach the screen texture when flushed
  (`GSPGPU_FlushDataCache`, `gfxFlushBuffers`, `gfxSwapBuffers`, shadow compare
  as a fallback). `C3D_SyncDisplayTransfer` from a render target into a
  framebuffer is a GPU blit into the screen texture, ordered after pending CPU
  writes to that screen.
- **Pacing**: one emulated VBlank per 1/59.83 s regardless of the display's
  refresh rate.
