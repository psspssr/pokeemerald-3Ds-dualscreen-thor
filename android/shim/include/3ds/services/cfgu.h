#pragma once
#include <3ds/types.h>
/* Device profiling only: no Nintendo CFG service is available. */
static inline Result cfguInit(void) { return -1; }
static inline void cfguExit(void) {}
static inline Result CFGU_GetSystemModel(u8 *model) { if(model) *model=255; return -1; }
