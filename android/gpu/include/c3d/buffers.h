#pragma once
#include "types.h"
typedef struct { const void *data; int stride, count; u64 permutation; } C3D_BufCfg;
typedef struct { int bufCount; C3D_BufCfg buffers[12]; } C3D_BufInfo;
void BufInfo_Init(C3D_BufInfo *info);
int BufInfo_Add(C3D_BufInfo *info, const void *data, int stride, int count, u64 permutation);
C3D_BufInfo *C3D_GetBufInfo(void);
void C3D_SetBufInfo(C3D_BufInfo *info);
