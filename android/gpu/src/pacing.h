#pragma once
#include <stdbool.h>

typedef struct {
    unsigned speed, remaining;
} GpuFrameSchedule;

/* A group advances N simulation ticks but submits/paces only one picture.
 * Zero-initialization and every speed change start with a visible frame. */
bool gpuFrameDue(GpuFrameSchedule *schedule,unsigned speed);

/* Absolute deadline for one emulated VBlank. A return value at or before now
 * means the frame is already overdue and must not sleep again. */
double gpuPacingDeadline(double now,double previous,double period);
