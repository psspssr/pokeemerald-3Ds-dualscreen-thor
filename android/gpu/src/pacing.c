#include "pacing.h"

bool gpuFrameDue(GpuFrameSchedule *schedule,unsigned speed)
{
    if(speed<1 || speed>4) speed=1;
    if(schedule->speed!=speed) {
        schedule->speed=speed;
        schedule->remaining=0;
    }
    if(schedule->remaining) { --schedule->remaining; return false; }
    schedule->remaining=speed-1;
    return true;
}

double gpuPacingDeadline(double now,double previous,double period)
{
    if(previous==0) return now+period;
    /* Keep the clock's phase through brief jitter. If several frames were
     * missed, resync at completion of this frame without adding another wait. */
    if(now>previous+4*period) return now;
    return previous+period;
}
