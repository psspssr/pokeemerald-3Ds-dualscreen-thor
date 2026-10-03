/* Actual AAudio backend and pthread recovery loop, with deterministic platform
 * failures and stream callbacks. This does not emulate audible device output. */
#include <aaudio/AAudio.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "ndsp_backend.h"

struct AAudioStreamBuilder { AAudioStream_errorCallback error; void *user; };
struct AAudioStream { AAudioStream_errorCallback error; void *user; };
typedef struct { AAudioStream_errorCallback callback; AAudioStream *stream; void *user; } Notice;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static unsigned attempts, starts, startCalls, closes, failOpens, failStartCall;
static unsigned holdCall1, holdCall2, enteredCall, releasedCall, timedWaits, timedReturns;
static int lastTimedResult;
static double openedAt[128];
/* Deliberately reuse the same address, so a pointer-only identity fix fails. */
static AAudioStream storage, *live;
static const CtrNdspBackend *backend;

static double nowMs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}

static void sleepMs(int ms)
{
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&t, &t) != 0) assert(errno == EINTR);
}

static struct timespec deadline(int ms)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_nsec += (long)ms * 1000000;
    t.tv_sec += t.tv_nsec / 1000000000;
    t.tv_nsec %= 1000000000;
    return t;
}

static bool waitCount(unsigned *value, unsigned target, int ms)
{
    struct timespec end = deadline(ms);
    pthread_mutex_lock(&lock);
    while (*value < target && pthread_cond_timedwait(&cond, &lock, &end) != ETIMEDOUT) {}
    bool ok = *value >= target;
    pthread_mutex_unlock(&lock);
    return ok;
}

/* Only the backend object redirects this POSIX function. The wait remains real;
 * observing its entry/result verifies pause/close wake it instead of timing out. */
int TestAudioTimedWait(pthread_cond_t *signal, pthread_mutex_t *mutex, const struct timespec *end)
{
    pthread_mutex_lock(&lock);
    ++timedWaits;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    int rc = pthread_cond_timedwait(signal, mutex, end);
    pthread_mutex_lock(&lock);
    ++timedReturns;
    lastTimedResult = rc;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    return rc;
}

void ShimLogf(int p, const char *format, ...)
{
    (void)p;
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}
void CtrNdsp_Render(float *out, int frames, int rate) { (void)out; (void)frames; (void)rate; }
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **b)
{ *b = calloc(1, sizeof(**b)); assert(*b); return AAUDIO_OK; }
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *b) { free(b); return AAUDIO_OK; }
#define SETTER(name) void name(AAudioStreamBuilder *b, int32_t v) { (void)b; (void)v; }
SETTER(AAudioStreamBuilder_setDirection)
SETTER(AAudioStreamBuilder_setSharingMode)
SETTER(AAudioStreamBuilder_setPerformanceMode)
SETTER(AAudioStreamBuilder_setFormat)
SETTER(AAudioStreamBuilder_setChannelCount)
SETTER(AAudioStreamBuilder_setUsage)
SETTER(AAudioStreamBuilder_setContentType)
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *b, AAudioStream_dataCallback cb, void *u)
{ (void)b; (void)cb; (void)u; }
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *b, AAudioStream_errorCallback cb, void *u)
{ b->error = cb; b->user = u; }
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *b, AAudioStream **s)
{
    pthread_mutex_lock(&lock);
    assert(attempts < 128);
    openedAt[attempts++] = nowMs();
    if (failOpens) {
        --failOpens;
        *s = NULL;
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&lock);
        return AAUDIO_ERROR_UNAVAILABLE;
    }
    assert(!live);
    storage.error = b->error;
    storage.user = b->user;
    live = *s = &storage;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    return AAUDIO_OK;
}
int32_t AAudioStream_getFormat(AAudioStream *s) { (void)s; return AAUDIO_FORMAT_PCM_FLOAT; }
int32_t AAudioStream_getChannelCount(AAudioStream *s) { (void)s; return 2; }
int32_t AAudioStream_getFramesPerBurst(AAudioStream *s) { (void)s; return 192; }
int32_t AAudioStream_getSampleRate(AAudioStream *s) { (void)s; return 48000; }
int32_t AAudioStream_getPerformanceMode(AAudioStream *s) { (void)s; return AAUDIO_PERFORMANCE_MODE_LOW_LATENCY; }
int32_t AAudioStream_setBufferSizeInFrames(AAudioStream *s, int32_t n) { (void)s; return n; }
aaudio_result_t AAudioStream_requestStart(AAudioStream *s)
{
    pthread_mutex_lock(&lock);
    assert(s == live);
    unsigned call = ++startCalls;
    if (call == holdCall1 || call == holdCall2) {
        enteredCall = call;
        pthread_cond_broadcast(&cond);
        while (releasedCall < call) pthread_cond_wait(&cond, &lock);
    }
    if (call == failStartCall) {
        pthread_mutex_unlock(&lock);
        return AAUDIO_ERROR_DISCONNECTED;
    }
    ++starts;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    return AAUDIO_OK;
}
aaudio_result_t AAudioStream_requestStop(AAudioStream *s) { (void)s; return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestPause(AAudioStream *s) { (void)s; return AAUDIO_OK; }
aaudio_result_t AAudioStream_close(AAudioStream *s)
{
    pthread_mutex_lock(&lock);
    assert(s == live);
    live = NULL;
    ++closes;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    return AAUDIO_OK;
}
const char *AAudio_convertResultToText(aaudio_result_t r)
{ return r == AAUDIO_OK ? "OK" : "injected temporary device failure"; }

static Notice notice(void)
{
    pthread_mutex_lock(&lock);
    assert(live);
    Notice n = {live->error, live, live->user};
    pthread_mutex_unlock(&lock);
    return n;
}
static void disconnect(Notice n) { n.callback(n.stream, n.user, AAUDIO_ERROR_DISCONNECTED); }
static void reset(void)
{
    pthread_mutex_lock(&lock);
    assert(!live);
    attempts = starts = startCalls = closes = failOpens = failStartCall = 0;
    holdCall1 = holdCall2 = enteredCall = releasedCall = timedWaits = timedReturns = 0;
    lastTimedResult = -1;
    pthread_mutex_unlock(&lock);
}
static void failNext(unsigned count)
{ pthread_mutex_lock(&lock); failOpens = count; pthread_mutex_unlock(&lock); }
static unsigned attemptCount(void)
{ pthread_mutex_lock(&lock); unsigned n = attempts; pthread_mutex_unlock(&lock); return n; }

static void initialFailure(void)
{
    reset();
    failNext(1);
    assert(!backend->open());
    sleepMs(150);
    assert(attemptCount() == 1);
    backend->close();
    puts("PASS initial output failure retains the existing no-backend result");
}

static void repeatedRecovery(void)
{
    reset();
    assert(backend->open());
    failNext(3);
    disconnect(notice());
    assert(waitCount(&starts, 2, 2000));
    backend->close();
    assert(attempts == 5 && starts == 2 && closes == 2);
    for (unsigned i = 2; i < attempts; ++i) assert(openedAt[i] - openedAt[i - 1] >= 70);
    puts("PASS three transient reopen failures recover with rate-limited attempts");
}

static void pausedRecovery(void)
{
    reset();
    assert(backend->open());
    failNext(1000);
    disconnect(notice());
    assert(waitCount(&timedWaits, 1, 2000));
    backend->pause();
    assert(waitCount(&timedReturns, 1, 2000));
    unsigned stoppedAt = attemptCount();
    sleepMs(250);
    assert(attemptCount() == stoppedAt);
    failNext(0);
    backend->resume();
    assert(waitCount(&starts, 2, 2000));
    unsigned resumedAt = attemptCount();
    sleepMs(150);
    assert(attemptCount() == resumedAt);
    backend->close();
    assert(lastTimedResult == 0);
    puts("PASS pause interrupts retry, suppresses opens, and resume preserves one replacement");
}

static void resumeFailure(void)
{
    reset();
    assert(backend->open());
    backend->pause();
    pthread_mutex_lock(&lock);
    failStartCall = 2;
    failOpens = 1;
    pthread_mutex_unlock(&lock);
    backend->resume();
    assert(waitCount(&starts, 2, 2000));
    backend->close();
    assert(attempts == 3 && closes == 2);
    puts("PASS failed resume reopen also schedules automatic recovery");
}

static void closeDuringRetry(void)
{
    reset();
    assert(backend->open());
    failNext(1000);
    disconnect(notice());
    assert(waitCount(&timedWaits, 1, 2000));
    double start = nowMs();
    backend->close();
    double elapsed = nowMs() - start;
    unsigned closedAt = attemptCount();
    sleepMs(150);
    assert(attemptCount() == closedAt && timedReturns > 0 && lastTimedResult == 0);
    printf("PASS close interrupts scheduled retry (%.3fms), with no late reopen\n", elapsed);
}

static void *resumeThread(void *unused) { (void)unused; backend->resume(); return NULL; }
static void releaseCall(unsigned call)
{
    pthread_mutex_lock(&lock);
    releasedCall = call;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}

static void staleError(bool newErrorPending)
{
    reset();
    assert(backend->open());
    Notice old = notice();
    backend->pause();
    pthread_mutex_lock(&lock);
    holdCall1 = failStartCall = 2;
    holdCall2 = newErrorPending ? 3 : 0;
    pthread_mutex_unlock(&lock);
    pthread_t resuming;
    assert(pthread_create(&resuming, NULL, resumeThread, NULL) == 0);
    assert(waitCount(&enteredCall, 2, 2000));
    disconnect(old);
    releaseCall(2);
    if (newErrorPending) {
        assert(waitCount(&enteredCall, 3, 2000));
        Notice newer = notice();
        assert(newer.stream == old.stream && newer.user != old.user);
        disconnect(newer);
        disconnect(old); /* Must not overwrite recovery for the current serial. */
        releaseCall(3);
    }
    assert(pthread_join(resuming, NULL) == 0);
    if (newErrorPending) {
        assert(waitCount(&starts, 3, 2000));
        sleepMs(50);
        assert(attemptCount() == 3);
    } else {
        assert(!waitCount(&starts, 3, 200));
        assert(attemptCount() == 2);
    }
    backend->close();
    puts(newErrorPending
        ? "PASS stale callback cannot overwrite a newer failing stream's recovery"
        : "PASS queued disconnect preserves the healthy Resume replacement at a reused address");
}

int main(void)
{
    backend = CtrNdsp_GetBackend();
    initialFailure();
    repeatedRecovery();
    pausedRecovery();
    resumeFailure();
    closeDuringRetry();
    staleError(false);
    staleError(true);
    return 0;
}
