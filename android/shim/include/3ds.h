/**
 * @file 3ds.h
 * @brief libctru's umbrella header, for the Android port.
 *
 * android/shim/include provides the system half of libctru, android/gpu/include
 * the graphics half (gfx, GSP, GX, GPU enums, shaders); both directories are
 * on the include path.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <3ds/types.h>
#include <3ds/result.h>
#include <3ds/svc.h>
#include <3ds/os.h>
#include <3ds/synchronization.h>
#include <3ds/thread.h>

#include <3ds/gfx.h>
#include <3ds/console.h>

#include <3ds/allocator/linear.h>
#include <3ds/allocator/mappable.h>
#include <3ds/allocator/vram.h>

#include <3ds/services/apt.h>
#include <3ds/services/dsp.h>
#include <3ds/services/gspgpu.h>
#include <3ds/services/hid.h>

#include <3ds/gpu/enums.h>
#include <3ds/gpu/gpu.h>
#include <3ds/gpu/gx.h>
#include <3ds/gpu/shbin.h>
#include <3ds/gpu/shaderProgram.h>

#include <3ds/ndsp/ndsp.h>
#include <3ds/ndsp/channel.h>

#include <3ds/romfs.h>

#include <ctrshim_newlib.h>

#ifdef __cplusplus
}
#endif
