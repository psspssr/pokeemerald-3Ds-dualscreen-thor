#pragma once

/* Absolute deadline for one emulated VBlank. A return value at or before now
 * means the frame is already overdue and must not sleep again. */
double gpuPacingDeadline(double now,double previous,double period);
