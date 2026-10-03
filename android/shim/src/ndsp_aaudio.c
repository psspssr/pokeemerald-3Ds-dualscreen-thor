/*
 * NDSP's output: one AAudio stream, stereo float at the device's native
 * rate, low latency, shared, two bursts of buffering; ndsp.c renders into it
 * from the data callback.
 *
 * A disconnect (headphones unplugged, Bluetooth gone) arrives on AAudio's
 * error callback, where the stream may not be closed; a small thread does
 * the close and reopen instead. While the activity is paused the stream is
 * paused, and a stream that died meanwhile is reopened on resume.
 */
#include <aaudio/AAudio.h>
#include <pthread.h>
#include <stdatomic.h>

#include "ndsp_backend.h"
#include "shim_internal.h"

/* Guards the stream and the paused flag; never taken by AAudio callbacks. */
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static AAudioStream *sStream;
static atomic_bool sPaused;
static uintptr_t sNextStreamSerial;
static atomic_uintptr_t sStreamSerial;

/* The disconnect hand-over: the error callback only sets sRestart. */
static pthread_mutex_t sSignalLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sSignal;
static pthread_once_t sSignalOnce = PTHREAD_ONCE_INIT;
static bool sSignalReady, sRestart, sRetry, sQuit;
static uintptr_t sRestartSerial;
static pthread_t sRestarter;
static bool sRestarterRunning;

/* A route can be temporarily unavailable after a disconnect. Retry without
 * spinning, using a monotonic and interruptible wait on the recovery thread. */
#define RETRY_DELAY_NS 100000000ULL

static void InitSignal(void)
{
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0)
        return;
    if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0)
        sSignalReady = pthread_cond_init(&sSignal, &attr) == 0;
    pthread_condattr_destroy(&attr);
}

static void ScheduleRetry(void)
{
    pthread_mutex_lock(&sSignalLock);
    if (!sQuit)
    {
        sRetry = true;
        pthread_cond_signal(&sSignal);
    }
    pthread_mutex_unlock(&sSignalLock);
}

static aaudio_data_callback_result_t DataCallback(AAudioStream *stream, void *user, void *audioData,
                                                  int32_t numFrames)
{
    (void)user;
    CtrNdsp_Render(audioData, numFrames, AAudioStream_getSampleRate(stream));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void ErrorCallback(AAudioStream *stream, void *user, aaudio_result_t error)
{
    (void)stream;
    uintptr_t serial = (uintptr_t)user;
    ShimLogf(ANDROID_LOG_WARN, "AAudio: stream error %s", AAudio_convertResultToText(error));
    pthread_mutex_lock(&sSignalLock);
    /* Close/reopen can overlap a queued error callback. Check identity while
     * holding the queue lock, so an older error cannot replace a newer one. */
    if (serial != 0 && serial == atomic_load(&sStreamSerial))
    {
        sRestart = true;
        sRestartSerial = serial;
        pthread_cond_signal(&sSignal);
    }
    pthread_mutex_unlock(&sSignalLock);
}

/* With sLock held. */
static void CloseStream(void)
{
    atomic_store(&sStreamSerial, 0);
    if (sStream != NULL)
    {
        AAudioStream_requestStop(sStream);
        AAudioStream_close(sStream);
        sStream = NULL;
    }
}

/* With sLock held. */
static bool OpenStream(void)
{
    AAudioStreamBuilder *builder = NULL;
    AAudioStream *stream = NULL;
    aaudio_result_t rc;
    int32_t burst;

    rc = AAudio_createStreamBuilder(&builder);
    if (rc != AAUDIO_OK)
    {
        ShimLogf(ANDROID_LOG_ERROR, "AAudio: no stream builder (%s)", AAudio_convertResultToText(rc));
        return false;
    }
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MUSIC);
    AAudioStreamBuilder_setDataCallback(builder, DataCallback, NULL);
    uintptr_t serial = ++sNextStreamSerial;
    if (serial == 0) serial = ++sNextStreamSerial;
    /* Publish before open/start can invoke its callback. Serials also avoid
     * mistaking a replacement allocated at the same address for the old one. */
    atomic_store(&sStreamSerial, serial);
    AAudioStreamBuilder_setErrorCallback(builder, ErrorCallback, (void *)serial);
    rc = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (rc != AAUDIO_OK)
    {
        atomic_store(&sStreamSerial, 0);
        ShimLogf(ANDROID_LOG_ERROR, "AAudio: cannot open a stream (%s)", AAudio_convertResultToText(rc));
        return false;
    }
    if (AAudioStream_getFormat(stream) != AAUDIO_FORMAT_PCM_FLOAT || AAudioStream_getChannelCount(stream) != 2)
    {
        atomic_store(&sStreamSerial, 0);
        ShimLogf(ANDROID_LOG_ERROR, "AAudio: stream is not stereo float");
        AAudioStream_close(stream);
        return false;
    }
    burst = AAudioStream_getFramesPerBurst(stream);
    if (burst > 0)
        AAudioStream_setBufferSizeInFrames(stream, burst * 2);
    rc = AAudioStream_requestStart(stream);
    if (rc != AAUDIO_OK)
    {
        atomic_store(&sStreamSerial, 0);
        ShimLogf(ANDROID_LOG_ERROR, "AAudio: cannot start (%s)", AAudio_convertResultToText(rc));
        AAudioStream_close(stream);
        return false;
    }
    ShimLogf(ANDROID_LOG_INFO, "AAudio: %d Hz, burst %d, %s", AAudioStream_getSampleRate(stream), burst,
             AAudioStream_getPerformanceMode(stream) == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY ? "low latency"
                                                                                           : "normal latency");
    sStream = stream;
    return true;
}

static void *Restarter(void *arg)
{
    (void)arg;
    for (;;)
    {
        pthread_mutex_lock(&sSignalLock);
        while (!sRestart && !sRetry && !sQuit)
            pthread_cond_wait(&sSignal, &sSignalLock);
        if (sRetry && !sQuit && !sPaused)
        {
            uint64_t at = ShimNowNs() + RETRY_DELAY_NS;
            struct timespec deadline = {(time_t)(at / 1000000000ULL), (long)(at % 1000000000ULL)};
            while (!sQuit && !sPaused)
                if (pthread_cond_timedwait(&sSignal, &sSignalLock, &deadline) == ETIMEDOUT)
                    break;
        }
        if (sQuit)
        {
            pthread_mutex_unlock(&sSignalLock);
            return NULL;
        }
        bool restart = sRestart;
        uintptr_t failedSerial = sRestartSerial;
        sRestart = sRetry = false;
        pthread_mutex_unlock(&sSignalLock);

        pthread_mutex_lock(&sLock);
        if (restart && failedSerial == atomic_load(&sStreamSerial))
            CloseStream();
        /* A successful Resume may already have reopened it while this
         * thread waited. A retry must not close that healthy replacement. */
        bool retry = !sPaused && sStream == NULL && !OpenStream();
        pthread_mutex_unlock(&sLock);
        if (retry)
            ScheduleRetry();
    }
}

static bool Open(void)
{
    bool ok;

    pthread_once(&sSignalOnce, InitSignal);
    if (!sSignalReady)
    {
        ShimLogf(ANDROID_LOG_ERROR, "AAudio: cannot initialize stream recovery signal");
        return false;
    }
    pthread_mutex_lock(&sSignalLock);
    sRestart = sRetry = sQuit = false;
    pthread_mutex_unlock(&sSignalLock);
    pthread_mutex_lock(&sLock);
    sPaused = false;
    ok = OpenStream();
    pthread_mutex_unlock(&sLock);
    if (!ok)
        return false;
    sRestarterRunning = pthread_create(&sRestarter, NULL, Restarter, NULL) == 0;
    if (!sRestarterRunning)
    {
        ShimLogf(ANDROID_LOG_ERROR, "AAudio: cannot create stream recovery thread");
        pthread_mutex_lock(&sLock);
        CloseStream();
        pthread_mutex_unlock(&sLock);
    }
    return sRestarterRunning;
}

static void Close(void)
{
    if (sRestarterRunning)
    {
        pthread_mutex_lock(&sSignalLock);
        sQuit = true;
        pthread_cond_signal(&sSignal);
        pthread_mutex_unlock(&sSignalLock);
        pthread_join(sRestarter, NULL);
        sRestarterRunning = false;
    }
    pthread_mutex_lock(&sLock);
    CloseStream();
    pthread_mutex_unlock(&sLock);
}

static void Pause(void)
{
    pthread_mutex_lock(&sLock);
    sPaused = true;
    if (sStream != NULL)
        AAudioStream_requestPause(sStream);
    pthread_mutex_unlock(&sLock);
    pthread_mutex_lock(&sSignalLock);
    pthread_cond_signal(&sSignal);
    pthread_mutex_unlock(&sSignalLock);
}

static void Resume(void)
{
    bool retry = false;
    pthread_mutex_lock(&sLock);
    sPaused = false;
    if (sStream != NULL && AAudioStream_requestStart(sStream) != AAUDIO_OK)
        CloseStream();
    if (sStream == NULL)
        retry = !OpenStream();
    pthread_mutex_unlock(&sLock);
    if (retry)
        ScheduleRetry();
}

static const CtrNdspBackend sBackend = {Open, Close, Pause, Resume};

const CtrNdspBackend *CtrNdsp_GetBackend(void)
{
    return &sBackend;
}
