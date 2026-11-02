#pragma once
#include <3ds/types.h>
#include <3ds/gpu/enums.h>
#include <3ds/gpu/registers.h>

void GPUCMD_AddWrite(u32 reg,u32 value);
void GPUCMD_AddMaskedWrite(u32 reg,u32 mask,u32 value);
