#include "pacing.h"

double gpuPacingDeadline(double now,double previous,double period)
{
    if(previous==0) return now+period;
    /* Keep the clock's phase through brief jitter. If several frames were
     * missed, resync at completion of this frame without adding another wait. */
    if(now>previous+4*period) return now;
    return previous+period;
}
