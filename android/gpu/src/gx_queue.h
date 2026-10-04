#pragma once
#include <3ds/gpu/gx.h>

/* Count immediate GLES transfers/splits for the upstream upload limiter. */
void gpuGxRecordCommand(void);
