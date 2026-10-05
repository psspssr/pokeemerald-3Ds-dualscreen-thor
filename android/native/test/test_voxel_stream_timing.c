#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <ctr_host.h>
#include "pacing.h"

typedef uint8_t u8;
#define GFX_LEFT 0
#define GFX_TOP 0
#define SYSCLOCK_ARM11 1000000000.0
#define PORT_PROF_BEGIN(name) ((void)0)
#define PORT_PROF_END(name, bucket) ((void)0)
typedef enum { CTR_APT_SUSPEND, CTR_APT_RESUME, CTR_APT_EXIT } CtrAptEvent;
typedef struct { unsigned width, height; } FrameBuf;
typedef struct { bool linked, used; int side, screen; FrameBuf frameBuf; } Target;
typedef struct GpuTarget { struct GpuTarget *next; Target *target; } GpuTarget;
static GpuTarget *gpuTargets;
static struct { unsigned numEntries, curEntry, lastEntry; } frameQueue;
static double nowSeconds, nextVblank, frameStart, drawingTime;
static const double framePeriod = 1.0 / 59.83;
static GpuFrameSchedule frameSchedule;
static bool initialized = true;
static CtrHostState state = CTR_HOST_RUNNING, stateAfterPresent = CTR_HOST_RUNNING;
static unsigned speed = 1, frameCount, sleeps, presents, jobs, atlasCalls, workCalls;
static double lastSleepTarget, lastWorkStart, drawSeconds;
static void (*endHook)(void *);
static void *endHookParam;
static struct { unsigned frames; float cpuMs, gpuMs; } sStats;
static unsigned sFpsFrames;
typedef struct { float gameMs, vblankMs; } CtrTiming;
static CtrTiming timing;
static struct { float afterMs, afterBudgetMs; } voxelStats;
static bool sReady = true, sViewReady, sCrossingSoon;
static unsigned sFrame, sAheadBackoff;
static float sLastBuildMs;
static struct { bool forView; } sAtlasJob;
static struct { bool active; } sJob;

static double gpuNow(void) { return nowSeconds; }
static uint64_t svcGetSystemTick(void) { return (uint64_t)llround(nowSeconds * SYSCLOCK_ARM11); }
CtrHostState CtrHost_GetState(void) { return state; }
unsigned CtrHost_GameSpeed(void) { return speed; }
static bool gpuInit(void) { return initialized; }
static void gpuC2DFlush(void) {}
static void gpuC2DResetFrame(void) {}
static void destroyWindow(int i) { (void)i; }
static void CtrDiagnostics_ResetClock(void) {}
static void gpuTransferToScreen(GpuTarget *t, int s, unsigned w, unsigned h)
{ (void)t; (void)s; (void)w; (void)h; }
static void gpuPresent(void) { ++presents; nowSeconds += .001; state = stateAfterPresent; }
static int fakeSleep(clockid_t clock, int flags, const struct timespec *deadline, struct timespec *remain)
{
    (void)remain; assert(clock == CLOCK_MONOTONIC && flags == TIMER_ABSTIME);
    lastSleepTarget = deadline->tv_sec + deadline->tv_nsec * 1e-9;
    assert(lastSleepTarget >= nowSeconds - 1e-8);
    nowSeconds = lastSleepTarget; ++sleeps; return 0;
}
#define clock_nanosleep fakeSleep
#include "display.inc"
#undef clock_nanosleep
void C3D_FrameEndHook(void (*hook)(void *), void *parameter) { endHook = hook; endHookParam = parameter; }
static float C3D_GetDrawingTime(void) { return (float)drawingTime; }
#include "core.inc"

static const CtrTiming *CtrPlatform_GetTiming(void) { return &timing; }
static void VoxelWorld_BeginBatch(void) { ++workCalls; lastWorkStart = nowSeconds; }
static void RunAtlasJob(uint64_t start, float budget, bool inFrame)
{ (void)start; assert(!inFrame && budget >= .5f); ++atlasCalls; }
static bool JobValid(void) { return true; }
static void JobCancel(void) { sJob.active = false; }
static void RunJobs(uint64_t start, float budget, float ahead, bool inFrame, bool force, void *missing)
{
    (void)start; (void)missing; assert(!inFrame && !force && ahead <= budget);
    // A full bounded slice makes a false extra-frame budget observable.
    ++jobs; nowSeconds += budget / 1000.0;
}
#define sStats voxelStats
#include "voxel.inc"
#undef sStats
#include "video_hook.inc"

static bool render(bool overworld, bool battleUpdated)
{
    nowSeconds += timing.gameMs / 1000.0;
    if (!C3D_FrameBegin(0)) return false;
    uint64_t start = svcGetSystemTick();
    sViewReady = overworld || battleUpdated;
    nowSeconds += drawSeconds;
#include "video_tail.inc"
    return true;
}
static void shutdownHook(void)
{
#include "shutdown_hook.inc"
}
static void unusedHook(void *parameter) { (void)parameter; }
static void reset(void)
{
    nowSeconds = nextVblank = 100.0; drawSeconds = .003;
    frameSchedule = (GpuFrameSchedule){0}; speed = 1;
    state = stateAfterPresent = CTR_HOST_RUNNING; initialized = true;
    timing = (CtrTiming){1.0f, 0.0f};
    frameCount = sleeps = presents = jobs = atlasCalls = workCalls = 0;
    lastSleepTarget = lastWorkStart = 0;
    sViewReady = sCrossingSoon = false; sReady = true;
    sFrame = 10; sAheadBackoff = 0; sLastBuildMs = 0;
    C3D_FrameEndHook(NULL, NULL);
}
static void checkFrame(bool overworld, bool battle)
{
    reset(); assert(render(overworld, battle));
    printf("stream frame: budget=%.3fms jobs=%u workAt=%.3fms end=%.3fms\n",
           voxelStats.afterBudgetMs, jobs, jobs ? (lastWorkStart - 100) * 1000 : 0, (nowSeconds - 100) * 1000);
    fflush(stdout);
    assert(jobs == 1 && atlasCalls == 1 && workCalls == 1);
    assert(voxelStats.afterBudgetMs > .5f && lastWorkStart < lastSleepTarget);
    assert(!endHook && !endHookParam && !sViewReady);
    assert(fabs(nowSeconds - (100 + framePeriod)) < .00001);
}
int main(void)
{
    checkFrame(true, false); checkFrame(false, true);
    assert(render(false, false)); assert(jobs == 1 && !endHook); // no stale voxel callback in a menu
    reset(); assert(render(false, false)); assert(!jobs && !endHook && sleeps == 1);
    for (unsigned i = 0; i < 4; ++i) {
        reset(); speed = 1u << i;
        for (unsigned tick = 0; tick < speed * 8; ++tick) {
            unsigned previousJobs = jobs;
            bool shown = render(true, false);
            assert(shown == (tick % speed == 0));
            assert(jobs == previousJobs + shown && !endHook && !endHookParam);
            if (shown) {
                assert(fabs(nowSeconds - (100 + framePeriod * presents)) < .00001);
                assert(voxelStats.afterBudgetMs >= .5f);
            }
        }
        assert(presents == 8 && sleeps == 8 && jobs == 8);
    }
    // A speed change uses the existing new anchor, not the old group's time.
    reset(); assert(render(true, false)); speed = 8; assert(render(true, false));
    assert(!render(true, false)); speed = 2; assert(render(true, false));
    assert(jobs == 3 && !endHook);
    // No invented budget while uninitialized, suspended or already late.
    reset();
    double anchor = nextVblank, clock = nowSeconds;
    assert(CtrGpu_FrameTimeLeftMs() > 16.7f && CtrGpu_FrameTimeLeftMs() < 16.8f);
    assert(nextVblank == anchor && nowSeconds == clock); // budget reads never move the deadline
    initialized = false; assert(CtrGpu_FrameTimeLeftMs() == 0);
    assert(!render(true, false) && !endHook);
    initialized = true; nextVblank = 0; assert(CtrGpu_FrameTimeLeftMs() == 0);
    nextVblank = 100; nowSeconds = 100 + 2 * framePeriod; assert(CtrGpu_FrameTimeLeftMs() == 0);
    nowSeconds = 100 + 5 * framePeriod; assert(CtrGpu_FrameTimeLeftMs() == 0);
    reset(); drawSeconds = .020; assert(render(true, false)); assert(!jobs && !sleeps && !endHook);
    // Model a UI pause/exit arriving during presentation, not a driver error.
    for (unsigned interruption = 0; interruption < 2; ++interruption) {
        reset(); stateAfterPresent = interruption ? CTR_HOST_EXITING : CTR_HOST_PAUSED;
        assert(render(true, false)); assert(!jobs && !endHook && !sViewReady);
    }
    reset(); lifecycle(CTR_APT_SUSPEND, NULL); assert(CtrGpu_FrameTimeLeftMs() == 0);
    nowSeconds = 150; lifecycle(CTR_APT_RESUME, NULL); assert(render(true, false));
    assert(jobs == 1 && fabs(nowSeconds - (150 + framePeriod)) < .00001);
    // Crossing reserve is applied once to each estimate, not twice overall.
    reset(); sCrossingSoon = true; assert(render(true, false));
    assert(voxelStats.afterBudgetMs > 4.0f && voxelStats.afterBudgetMs < 5.0f && jobs == 1);
    reset(); speed = 8; assert(render(true, false));
    for (unsigned tick = 1; tick < 8; ++tick) assert(!render(true, false));
    sCrossingSoon = true; assert(render(true, false));
    assert(jobs == 1 && voxelStats.afterBudgetMs == 0); // skipped ticks cannot spend the crossing reserve
    reset(); C3D_FrameEndHook(unusedHook, &timing);
    shutdownHook(); assert(!endHook && !endHookParam);
    puts("PASS real frame-end / voxel budget: prefetch before pace,1/2/4/8x, skip/speed change, overdue, suspend/resume, pause/exit during presentation and callback cleanup");
    return 0;
}
