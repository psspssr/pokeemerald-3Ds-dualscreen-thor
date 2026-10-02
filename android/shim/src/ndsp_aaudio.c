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

#include "ndsp_backend.h"
#include "shim_internal.h"

/* Guards the stream and the paused flag; never taken by AAudio callbacks. */
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static AAudioStream *sStream;
static bool sPaused;

/* The disconnect hand-over: the error callback only sets sRestart. */
static pthread_mutex_t sSignalLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sSignal = PTHREAD_COND_INITIALIZER;
static bool sRestart, sQuit;
static pthread_t sRestarter;
static bool sRestarterRunning;

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
    (void)user;
    ShimLogf(ANDROID_LOG_WARN, "AAudio: stream error %s", AAudio_convertResultToText(error));
    pthread_mutex_lock(&sSignalLock);
    sRestart = true;
    pthread_cond_signal(&sSignal);
    pthread_mutex_unlock(&sSignalLock);
}

/* With sLock held. */
static void CloseStream(void)
{
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
    AAudioStreamBuilder_setErrorCallback(builder, ErrorCallback, NULL);
    rc = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (rc != AAUDIO_OK)
    {
        ShimLogf(ANDROID_LOG_ERROR, "AAudio: cannot open a stream (%s)", AAudio_convertResultToText(rc));
        return false;
    }
    if (AAudioStream_getFormat(stream) != AAUDIO_FORMAT_PCM_FLOAT || AAudioStream_getChannelCount(stream) != 2)
    {
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
        while (!sRestart && !sQuit)
            pthread_cond_wait(&sSignal, &sSignalLock);
        if (sQuit)
        {
            pthread_mutex_unlock(&sSignalLock);
            return NULL;
        }
        sRestart = false;
        pthread_mutex_unlock(&sSignalLock);

        pthread_mutex_lock(&sLock);
        CloseStream();
        if (!sPaused)
            OpenStream();
        pthread_mutex_unlock(&sLock);
    }
}

static bool Open(void)
{
    bool ok;

    pthread_mutex_lock(&sSignalLock);
    sRestart = sQuit = false;
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
}

static void Resume(void)
{
    pthread_mutex_lock(&sLock);
    sPaused = false;
    if (sStream != NULL && AAudioStream_requestStart(sStream) != AAUDIO_OK)
        CloseStream();
    if (sStream == NULL)
        OpenStream();
    pthread_mutex_unlock(&sLock);
}

static const CtrNdspBackend sBackend = {Open, Close, Pause, Resume};

const CtrNdspBackend *CtrNdsp_GetBackend(void)
{
    return &sBackend;
}
