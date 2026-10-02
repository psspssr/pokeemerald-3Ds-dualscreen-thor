# Third-party code in android/shim

All of it comes from [libctru](https://github.com/devkitPro/libctru)
(commit 9b55eda44cf80b971503991e0c78f9bc8fe50425), © devkitPro and the
libctru contributors, under the zlib licence:

> This software is provided 'as-is', without any express or implied
> warranty. In no event will the authors be held liable for any damages
> arising from the use of this software.
>
> Permission is granted to anyone to use this software for any purpose,
> including commercial applications, and to alter it and redistribute it
> freely, subject to the following restrictions:
>
> 1. The origin of this software must not be misrepresented; you must not
>    claim that you wrote the original software. If you use this software in
>    a product, an acknowledgment in the product documentation would be
>    appreciated but is not required.
> 2. Altered source versions must be plainly marked as such, and must not be
>    misrepresented as being the original software.
> 3. This notice may not be removed or altered from any source distribution.

| File | Taken from | Changes |
|---|---|---|
| `include/3ds/**/*.h`, `include/3ds.h` | `libctru/include/3ds/...` | Re-typed subsets: only the declarations the port needs, same names, values and struct layouts. Inline assembly replaced by compiler builtins; documentation rewritten for the Android behaviour. |
| `include/sys/iosupport.h` | devkitARM newlib `sys/iosupport.h` (newlib licence, BSD-style) | Declarations only. |
| `src/console.c` | `libctru/source/console.c` | Pointer casts made 64-bit clean, per-write `GSPGPU_FlushDataCache` of touched columns, logcat copy, bounds check on escape arguments. |
| `src/default_font.h` | `libctru/data/default_font.bin` | Converted to a C array. |
| `src/mem.c` (the `Pool*` functions) | `libctru/source/allocator/mem_pool.cpp`, `linear.cpp`, `vram.cpp` | Ported from C++ to C, operating on virtual `u32` addresses. |
| `src/sync.c`, `src/thread.c`, `src/hid.c`, `src/apt.c`, `src/ndsp.c` | `libctru/source/synchronization.c`, `thread.c`, `services/hid.c`, `services/apt.c`, `ndsp/*.c` | Re-implemented (futexes, pthreads, the host input, the activity lifecycle, AAudio) following libctru's semantics; small pieces of logic (HID edge detection, wave buffer bookkeeping, LightEvent states) mirror the originals. |
