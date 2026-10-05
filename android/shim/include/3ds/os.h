/**
 * @file os.h
 * @brief libctru's OS constants and helpers (zlib licence, devkitPro), for
 * the Android shim.
 */
#pragma once

#include <3ds/types.h>
#include <3ds/svc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYSCLOCK_SOC (16756991u)
#define SYSCLOCK_SYS (SYSCLOCK_SOC * 2)
#define SYSCLOCK_SDMMC (SYSCLOCK_SYS * 2)
#define SYSCLOCK_ARM9 (SYSCLOCK_SYS * 4)
#define SYSCLOCK_ARM11 (SYSCLOCK_ARM9 * 2)
#define SYSCLOCK_ARM11_LGR1 (SYSCLOCK_ARM11 * 2)
#define SYSCLOCK_ARM11_LGR2 (SYSCLOCK_ARM11 * 3)
#define SYSCLOCK_ARM11_NEW SYSCLOCK_ARM11_LGR2

#define CPU_TICKS_PER_MSEC (SYSCLOCK_ARM11 / 1000.0)
#define CPU_TICKS_PER_USEC (SYSCLOCK_ARM11 / 1000000.0)

#define SYSTEM_VERSION(major, minor, revision) (((major) << 24) | ((minor) << 16) | ((revision) << 8))

#define OS_HEAP_AREA_BEGIN 0x08000000
#define OS_HEAP_AREA_END 0x0E000000
#define OS_MAP_AREA_BEGIN 0x10000000
#define OS_MAP_AREA_END 0x14000000

#define OS_OLD_FCRAM_VADDR 0x14000000
#define OS_OLD_FCRAM_PADDR 0x20000000
#define OS_OLD_FCRAM_SIZE 0x8000000

/* Android's bounded graphics-backing quota is larger than the console's.
 * This is an accounting address only, never a fixed Android mapping. Keep
 * its 16 MiB range separate from old FCRAM, DSP and shared configuration. */
#define OS_VRAM_VADDR 0x1E000000
#define OS_VRAM_PADDR 0x18000000
#define OS_VRAM_SIZE 0x1000000

#define OS_DSPRAM_VADDR 0x1FF00000
#define OS_DSPRAM_PADDR 0x1FF00000
#define OS_DSPRAM_SIZE 0x80000

/* Console address only. Android has no Luma/3DS shared configuration page;
 * svcGetSystemInfo rejects that capability before upstream accesses it. */
#define OS_SHAREDCFG_VADDR 0x1FF81000u

#define OS_FCRAM_VADDR 0x30000000
#define OS_FCRAM_PADDR 0x20000000
#define OS_FCRAM_SIZE 0x10000000

/*
 * The emulated physical address of a linearAlloc/vramAlloc block, as the GPU
 * would see it; 0 for memory outside both pools (as on hardware).
 */
u32 osConvertVirtToPhys(const void *vaddr);

/* Milliseconds since 1900-01-01, like the 3DS RTC (wall clock, local time). */
u64 osGetTime(void);

/* A phone has no parallax barrier: the slider is always down. */
static inline float osGet3DSliderState(void)
{
    return 0.0f;
}

static inline u8 osGetWifiStrength(void)
{
    return 3;
}

/* No effect: there is no clock to switch. */
void osSetSpeedupEnable(bool enable);

#ifdef __cplusplus
}
#endif
