/* Real host + APT pause ordering with a deliberately unfinished frame.
 * Platform window refcounts and the renderer's lifecycle release are the
 * only adapters. No Android device or generated game source is required. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <android/native_window.h>
#include <3ds/services/apt.h>
#include "ctr_host.h"
#include "ctrshim_apt.h"

struct ANativeWindow { atomic_int refs; };
static struct ANativeWindow window;
static struct ANativeWindow *drawingWindow;
static int windowIndex;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static bool drawing, finishFrame, rendererReleased, destroyStarted, destroyReturned, exited;

void ANativeWindow_acquire(ANativeWindow *w) { atomic_fetch_add(&w->refs, 1); }
void ANativeWindow_release(ANativeWindow *w) { assert(atomic_fetch_sub(&w->refs, 1) > 0); }
int __android_log_print(int p, const char *t, const char *f, ...)
{ (void)p; (void)t; (void)f; return 0; }
void ShimLogf(int p, const char *f, ...) { (void)p; (void)f; }
bool CtrHost_ShowShinyFleePrompt(uint32_t r) { (void)r; return false; }

static struct timespec deadline(int ms)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_nsec += (long)ms * 1000000;
    t.tv_sec += t.tv_nsec / 1000000000;
    t.tv_nsec %= 1000000000;
    return t;
}

static double nowMs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}

static void waitFlag(bool *flag)
{
    struct timespec end = deadline(2000);
    pthread_mutex_lock(&lock);
    while (!*flag) assert(pthread_cond_timedwait(&cond, &lock, &end) == 0);
    pthread_mutex_unlock(&lock);
}

static void lifecycle(CtrAptEvent event, void *user)
{
    (void)user;
    if (event == CTR_APT_SUSPEND) {
        ANativeWindow_release(drawingWindow);
        drawingWindow = NULL;
        pthread_mutex_lock(&lock);
        drawing = false;
        rendererReleased = true;
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&lock);
    }
}

void CtrHost_NotifyGameExit(int status)
{
    assert(status == 0);
    pthread_mutex_lock(&lock);
    exited = true;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}

int TestGameMain(void)
{
    uint32_t generation;
    drawingWindow = CtrHost_AcquireWindowAt(windowIndex, &generation);
    assert(drawingWindow == &window);
    pthread_mutex_lock(&lock);
    drawing = true;
    pthread_cond_broadcast(&cond);
    while (!finishFrame) pthread_cond_wait(&cond, &lock);
    pthread_mutex_unlock(&lock);
    assert(!aptMainLoop());
    return 0;
}

static void *destroy(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&lock);
    destroyStarted = true;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    CtrHost_SetWindowAt(windowIndex, NULL);
    pthread_mutex_lock(&lock);
    destroyReturned = true;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    return NULL;
}

static void releaseFrame(void)
{
    pthread_mutex_lock(&lock);
    finishFrame = true;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    waitFlag(&rendererReleased);
    assert(CtrHost_WaitUntilPaused(2000));
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const char *mode = argv[1];
    windowIndex = atoi(argv[2]);
    assert(windowIndex >= 0 && windowIndex < CTR_HOST_MAX_WINDOWS);
    assert(CtrApt_AddListener(lifecycle, NULL));
    CtrHost_SetWindowAt(windowIndex, &window);
    assert(CtrHost_StartGame());
    waitFlag(&drawing);
    if (!strcmp(mode, "settled")) {
        CtrHost_SetState(CTR_HOST_PAUSED);
        releaseFrame();
        double start = nowMs();
        CtrHost_SetWindowAt(windowIndex, NULL);
        double elapsed = nowMs() - start;
        /* No future rendering/acquisition can occur while paused. Waiting
         * for the new generation here would always consume the250ms cap. */
        assert(elapsed < 150);
        printf("PASS window%d already-paused detach completed in %.3fms\n", windowIndex, elapsed);
    } else {
        bool before = !strcmp(mode, "before");
        assert(before || !strcmp(mode, "during"));
        if (before) CtrHost_SetState(CTR_HOST_PAUSED);
        pthread_t ui;
        assert(pthread_create(&ui, NULL, destroy, NULL) == 0);
        waitFlag(&destroyStarted);
        if (!before) {
            double end = nowMs() + 1000;
            while (CtrHost_WindowGenerationAt(windowIndex) != 2 && nowMs() < end) {
                struct timespec tick = {0, 1000000};
                nanosleep(&tick, NULL);
            }
            assert(CtrHost_WindowGenerationAt(windowIndex) == 2);
            CtrHost_SetState(CTR_HOST_PAUSED);
        }
        pthread_mutex_lock(&lock);
        struct timespec end = deadline(50);
        while (!destroyReturned && pthread_cond_timedwait(&cond, &lock, &end) != ETIMEDOUT) {}
        bool returnedWhileDrawing = destroyReturned && drawing;
        pthread_mutex_unlock(&lock);
        releaseFrame();
        assert(pthread_join(ui, NULL) == 0);
        assert(!returnedWhileDrawing);
        printf("PASS window%d pause-%s-detach waited for actual renderer release\n", windowIndex, mode);
    }
    CtrHost_SetState(CTR_HOST_EXITING);
    waitFlag(&exited);
    assert(atomic_load(&window.refs) == 0);
    return 0;
}
