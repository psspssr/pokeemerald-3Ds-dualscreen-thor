# Graphics backend notices

The Android GLES implementation in `src/` is original port code under the
repository MIT license, except for the Citro3D mathematical routines listed
below. It implements the public libctru, Citro3D, Citro2D and Tex3DS interfaces
needed by the unchanged upstream game. It is not an official devkitPro port.

## Citro3D

Upstream: [devkitPro/citro3d](https://github.com/devkitPro/citro3d/tree/9f21cf7b380ce6f9e01a0420f19f0763e5443ca7).
License: zlib, Copyright (C) 2014–2018 fincs. The complete, unmodified notice
is in [licenses/citro3d.txt](licenses/citro3d.txt).

The following files were byte-compared against commit
`9f21cf7b380ce6f9e01a0420f19f0763e5443ca7` on 2026-10-02:

- All 30 `src/maths/*.c` files and `src/mtxstack.c` are unchanged copies of
  upstream `source/maths/*.c` and `source/mtxstack.c`.
- `include/c3d/attribs.h`, `effect.h`, `framebuffer.h`, `maths.h`,
  `mtxstack.h`, `proctex.h`, `renderqueue.h`, `texenv.h`, and `uniforms.h`
  are unchanged copies at the same upstream paths.
- `include/c3d/types.h` is an altered copy: it always includes the Android
  compatibility `3ds.h`, including when compiled by a host toolchain.
- `include/c3d/texture.h` is an altered copy: its alignment annotation uses
  the Android shim's `ALIGN(8)` spelling.

`include/c3d/buffers.h` and the GPU runtime are Android implementations of the
interfaces, with different internal storage. Their presence does not claim
compatibility with unimplemented Citro3D features or future upstream APIs.

## libctru

Upstream: [devkitPro/libctru](https://github.com/devkitPro/libctru/tree/9b55eda44cf80b971503991e0c78f9bc8fe50425).
License: zlib. The complete notice from the upstream README's License section
is preserved in [licenses/libctru.txt](licenses/libctru.txt). That section does
not give a copyright-holder line; no holder has been inferred here.

`include/3ds/gpu/enums.h` and `include/3ds/gpu/registers.h` were byte-compared
against upstream `libctru/include/3ds/gpu/` at commit
`9b55eda44cf80b971503991e0c78f9bc8fe50425` on 2026-10-02 and are unchanged.
The other libctru-shaped headers and implementations in this directory are
Android replacements.

## Citro2D

Upstream: [devkitPro/citro2d](https://github.com/devkitPro/citro2d/tree/147b02aae021da61b1f620446ad2892ecc45411e).
License: zlib, Copyright (C) 2017–2018 fincs. The complete, unmodified notice
is in [licenses/citro2d.txt](licenses/citro2d.txt).

`include/citro2d.h` adapts the public data types, color and image-tint helpers
from `include/c2d/base.h` to the Android subset. It is an altered interface
header. The independent GLES implementation in `src/citro2d.c` follows the
upstream negative-scale, scene-transform, tint, and clear-color conventions;
it does not contain the original Citro2D rendering implementation.
