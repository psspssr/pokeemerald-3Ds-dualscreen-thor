/*
 * NDSP: libctru's channel and wave buffer model, mixed on the output's audio
 * thread (see 3ds/ndsp/ndsp.h).
 *
 * One mutex guards the channels. The game thread holds it for a list
 * operation or a parameter store; the audio thread for one Render. It is a
 * priority-inheritance mutex, so the audio thread waiting on it lends its
 * real-time priority to whichever game thread holds it.
 *
 * Each channel resamples by linear interpolation between two source frames
 * a and b with a fractional position advanced by rate / output rate per
 * output frame. The rate is read every frame, so the small, frequent changes
 * 3ds_audio.c makes are seamless, and buffer boundaries are invisible: the
 * next buffer's first frame is fetched into b as the previous one ends.
 */
#include <3ds/ndsp/channel.h>
#include <3ds/ndsp/ndsp.h>
#include <3ds/result.h>
#include <3ds/services/dsp.h>
#include <pthread.h>
#include <string.h>
#include <ctr_host.h>

#include "ctrshim_apt.h"
#include "ndsp_backend.h"
#include "shim_internal.h"

#define CHANNEL_COUNT 24
/* Samples per DSP frame: ndspSetCallback's and ndspGetFrameCount's unit. */
#define DSP_FRAME_SAMPLES 160

typedef struct
{
    ndspWaveBuf *head;
    u32 pos;
    bool started;
    u16 format;
    ndspInterpType interp;
    float rate;
    float mix[12];
    bool paused;
    /* Resampler: output = a + (b - a) * frac. */
    double frac;
    float aL, aR, bL, bR;
    bool active;
    u16 seqNext, seqPlaying;
} Channel;

static pthread_once_t sOnce = PTHREAD_ONCE_INIT;
static pthread_mutex_t sLock;
static pthread_mutex_t sInitLock = PTHREAD_MUTEX_INITIALIZER;
static int sRefCount;
static const CtrNdspBackend *sBackend;

static Channel sChannels[CHANNEL_COUNT];
static float sMasterVol = 1.0f;
static ndspOutputMode sOutputMode = NDSP_OUTPUT_STEREO;
static ndspClippingMode sClipping = NDSP_CLIP_SOFT;
static int sOutputCount = 2;
static ndspCallback sCallback;
static void *sCallbackData;
static double sFrameSamples;
static volatile u32 sFrameCount;
static bool sAdpcmReported;
static bool sAccelerated;

static void InitLock(void)
{
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    if (pthread_mutex_init(&sLock, &attr) != 0)
        pthread_mutex_init(&sLock, NULL);
    pthread_mutexattr_destroy(&attr);
}

static void Lock(void)
{
    pthread_once(&sOnce, InitLock);
    pthread_mutex_lock(&sLock);
}

static void Unlock(void)
{
    pthread_mutex_unlock(&sLock);
}

static Channel *Get(int id)
{
    return id >= 0 && id < CHANNEL_COUNT ? &sChannels[id] : NULL;
}

static void SetStatus(ndspWaveBuf *buf, u8 status)
{
    __atomic_store_n(&buf->status, status, __ATOMIC_RELEASE);
}

/* The head is finished: on to the next buffer. buf is not touched after it
 * is marked done, since the game may reuse it at once. */
static void Advance(Channel *chn)
{
    ndspWaveBuf *buf = chn->head;

    chn->head = buf->next;
    chn->pos = 0;
    chn->started = false;
    SetStatus(buf, NDSP_WBUF_DONE);
}

static void DropQueue(Channel *chn)
{
    while (chn->head)
        Advance(chn);
    chn->frac = 0.0;
    chn->aL = chn->aR = chn->bL = chn->bR = 0.0f;
    chn->active = false;
}

/* Fast-forward still runs the game's mixer for each simulation tick, but
 * playing that backlog at normal speed would delay sound by seconds. Discard
 * transport buffers while accelerated, and clear resampler history on both
 * edges so returning to 1x starts with current audio. Called under sLock. */
static bool RefreshSpeedPolicy(void)
{
    bool accelerated = CtrHost_GameSpeed() > 1;

    if (accelerated != sAccelerated)
    {
        for (int id = 0; id < CHANNEL_COUNT; ++id)
            DropQueue(&sChannels[id]);
        sAccelerated = accelerated;
    }
    return accelerated;
}

/* The next source frame, or false if the queue is empty. */
static bool Fetch(Channel *chn, float *left, float *right)
{
    for (;;)
    {
        ndspWaveBuf *buf = chn->head;
        u32 encoding, channels;

        if (buf == NULL)
            return false;
        encoding = (chn->format >> 2) & 3;
        channels = chn->format & 3;
        if (encoding == NDSP_ENCODING_ADPCM || buf->data_vaddr == NULL || chn->pos >= buf->nsamples)
        {
            if (encoding == NDSP_ENCODING_ADPCM && !sAdpcmReported)
            {
                sAdpcmReported = true;
                ShimLogf(ANDROID_LOG_ERROR, "NDSP: ADPCM is not supported; buffers are skipped");
            }
            Advance(chn);
            continue;
        }
        if (!chn->started)
        {
            chn->started = true;
            chn->seqPlaying = buf->sequence_id;
            SetStatus(buf, NDSP_WBUF_PLAYING);
        }
        if (encoding == NDSP_ENCODING_PCM16)
        {
            if (channels == 2)
            {
                *left = buf->data_pcm16[chn->pos * 2] * (1.0f / 32768.0f);
                *right = buf->data_pcm16[chn->pos * 2 + 1] * (1.0f / 32768.0f);
            }
            else
                *left = *right = buf->data_pcm16[chn->pos] * (1.0f / 32768.0f);
        }
        else
        {
            if (channels == 2)
            {
                *left = buf->data_pcm8[chn->pos * 2] * (1.0f / 128.0f);
                *right = buf->data_pcm8[chn->pos * 2 + 1] * (1.0f / 128.0f);
            }
            else
                *left = *right = buf->data_pcm8[chn->pos] * (1.0f / 128.0f);
        }
        if (++chn->pos >= buf->nsamples)
        {
            if (buf->looping)
                chn->pos = 0;
            else
                Advance(chn);
        }
        return true;
    }
}

static void MixChannel(Channel *chn, float *out, int frames, int rate)
{
    double step = (double)chn->rate / (double)rate;
    float gainL = chn->mix[0] + chn->mix[2], gainR = chn->mix[1] + chn->mix[3];

    if (step <= 0.0)
        return;
    for (int i = 0; i < frames; ++i)
    {
        float t, left, right;

        while (chn->frac >= 1.0)
        {
            chn->frac -= 1.0;
            chn->aL = chn->bL;
            chn->aR = chn->bR;
            if (!Fetch(chn, &chn->bL, &chn->bR))
                chn->bL = chn->bR = 0.0f;
        }
        if (chn->interp == NDSP_INTERP_NONE)
        {
            left = chn->aL;
            right = chn->aR;
        }
        else
        {
            t = (float)chn->frac;
            left = chn->aL + (chn->bL - chn->aL) * t;
            right = chn->aR + (chn->bR - chn->aR) * t;
        }
        out[i * 2] += left * gainL;
        out[i * 2 + 1] += right * gainR;
        chn->frac += step;
    }
    if (chn->head == NULL && chn->aL == 0.0f && chn->aR == 0.0f && chn->bL == 0.0f && chn->bR == 0.0f)
        chn->active = false;
}

void CtrNdsp_Render(float *out, int frames, int rate)
{
    ndspCallback callback;
    void *callbackData;
    unsigned elapsed = 0;

    if (out == NULL || frames <= 0)
        return;
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    if (rate <= 0)
        return;
    Lock();
    bool muted = RefreshSpeedPolicy();
    for (int id = 0; id < CHANNEL_COUNT; ++id)
    {
        Channel *chn = &sChannels[id];

        if (!muted && !chn->paused && (chn->head != NULL || chn->active))
            MixChannel(chn, out, frames, rate);
    }
    for (int i = 0; i < frames; ++i)
    {
        float left = out[i * 2] * sMasterVol, right = out[i * 2 + 1] * sMasterVol;

        if (sOutputMode == NDSP_OUTPUT_MONO)
            left = right = (left + right) * 0.5f;
        out[i * 2] = left > 1.0f ? 1.0f : left < -1.0f ? -1.0f : left;
        out[i * 2 + 1] = right > 1.0f ? 1.0f : right < -1.0f ? -1.0f : right;
    }
    sFrameSamples += (double)frames * NDSP_SAMPLE_RATE / (double)rate;
    while (sFrameSamples >= DSP_FRAME_SAMPLES)
    {
        sFrameSamples -= DSP_FRAME_SAMPLES;
        ++elapsed;
    }
    sFrameCount += elapsed;
    callback = sCallback;
    callbackData = sCallbackData;
    Unlock();
    if (callback != NULL)
        while (elapsed--)
            callback(callbackData);
}

static void ResetChannel(Channel *chn)
{
    DropQueue(chn);
    chn->format = NDSP_FORMAT_PCM16;
    chn->interp = NDSP_INTERP_POLYPHASE;
    chn->rate = (float)NDSP_SAMPLE_RATE;
    memset(chn->mix, 0, sizeof(chn->mix));
    chn->mix[0] = chn->mix[1] = 1.0f;
    chn->paused = false;
    chn->seqNext = 0;
    chn->seqPlaying = 0;
}

static void AptListener(CtrAptEvent event, void *user)
{
    (void)user;
    if (sBackend == NULL)
        return;
    if (event == CTR_APT_RESUME)
        sBackend->resume();
    else
        sBackend->pause();
}

Result ndspInit(void)
{
    Result rc = 0;

    pthread_mutex_lock(&sInitLock);
    if (sRefCount++ == 0)
    {
        Lock();
        for (int id = 0; id < CHANNEL_COUNT; ++id)
            ResetChannel(&sChannels[id]);
        sMasterVol = 1.0f;
        sOutputMode = NDSP_OUTPUT_STEREO;
        sClipping = NDSP_CLIP_SOFT;
        sOutputCount = 2;
        sFrameSamples = 0.0;
        sAccelerated = false;
        Unlock();
        sBackend = CtrNdsp_GetBackend();
        if (sBackend == NULL || !sBackend->open())
        {
            ShimLogf(ANDROID_LOG_ERROR, "ndspInit: no audio output");
            sBackend = NULL;
            sRefCount = 0;
            rc = MAKERESULT(RL_PERMANENT, RS_NOTFOUND, RM_DSP, RD_NOT_FOUND);
        }
        else
            CtrApt_AddListener(AptListener, NULL);
    }
    pthread_mutex_unlock(&sInitLock);
    return rc;
}

void ndspExit(void)
{
    pthread_mutex_lock(&sInitLock);
    if (sRefCount > 0 && --sRefCount == 0)
    {
        CtrApt_RemoveListener(AptListener, NULL);
        sBackend->close();
        sBackend = NULL;
        Lock();
        for (int id = 0; id < CHANNEL_COUNT; ++id)
            DropQueue(&sChannels[id]);
        Unlock();
    }
    pthread_mutex_unlock(&sInitLock);
}

u32 ndspGetDroppedFrames(void)
{
    return 0;
}

u32 ndspGetFrameCount(void)
{
    return sFrameCount;
}

void ndspSetMasterVol(float volume)
{
    Lock();
    sMasterVol = volume;
    Unlock();
}

float ndspGetMasterVol(void)
{
    return sMasterVol;
}

void ndspSetOutputMode(ndspOutputMode mode)
{
    Lock();
    sOutputMode = mode;
    Unlock();
}

ndspOutputMode ndspGetOutputMode(void)
{
    return sOutputMode;
}

void ndspSetClippingMode(ndspClippingMode mode)
{
    sClipping = mode;
}

ndspClippingMode ndspGetClippingMode(void)
{
    return sClipping;
}

void ndspSetOutputCount(int count)
{
    sOutputCount = count;
}

int ndspGetOutputCount(void)
{
    return sOutputCount;
}

void ndspSetCallback(ndspCallback callback, void *data)
{
    Lock();
    sCallback = callback;
    sCallbackData = data;
    Unlock();
}

void ndspChnReset(int id)
{
    Channel *chn = Get(id);

    if (chn == NULL)
        return;
    Lock();
    ResetChannel(chn);
    Unlock();
}

void ndspChnInitParams(int id)
{
    (void)id;
}

bool ndspChnIsPlaying(int id)
{
    Channel *chn = Get(id);
    bool playing;

    if (chn == NULL)
        return false;
    Lock();
    playing = chn->head != NULL && !chn->paused;
    Unlock();
    return playing;
}

u32 ndspChnGetSamplePos(int id)
{
    Channel *chn = Get(id);
    u32 pos;

    if (chn == NULL)
        return 0;
    Lock();
    pos = chn->pos;
    Unlock();
    return pos;
}

u16 ndspChnGetWaveBufSeq(int id)
{
    Channel *chn = Get(id);
    u16 seq;

    if (chn == NULL)
        return 0;
    Lock();
    seq = chn->head != NULL ? chn->seqPlaying : 0;
    Unlock();
    return seq;
}

bool ndspChnIsPaused(int id)
{
    Channel *chn = Get(id);

    return chn != NULL && chn->paused;
}

void ndspChnSetPaused(int id, bool paused)
{
    Channel *chn = Get(id);

    if (chn == NULL)
        return;
    Lock();
    chn->paused = paused;
    Unlock();
}

void ndspChnSetFormat(int id, u16 format)
{
    Channel *chn = Get(id);

    if (chn == NULL)
        return;
    Lock();
    chn->format = format;
    Unlock();
}

u16 ndspChnGetFormat(int id)
{
    Channel *chn = Get(id);

    return chn != NULL ? chn->format : 0;
}

void ndspChnSetInterp(int id, ndspInterpType type)
{
    Channel *chn = Get(id);

    if (chn == NULL)
        return;
    Lock();
    chn->interp = type;
    Unlock();
}

ndspInterpType ndspChnGetInterp(int id)
{
    Channel *chn = Get(id);

    return chn != NULL ? chn->interp : NDSP_INTERP_POLYPHASE;
}

void ndspChnSetRate(int id, float rate)
{
    Channel *chn = Get(id);

    if (chn == NULL)
        return;
    Lock();
    chn->rate = rate;
    Unlock();
}

float ndspChnGetRate(int id)
{
    Channel *chn = Get(id);

    return chn != NULL ? chn->rate : 0.0f;
}

void ndspChnSetMix(int id, float mix[12])
{
    Channel *chn = Get(id);

    if (chn == NULL)
        return;
    Lock();
    memcpy(chn->mix, mix, sizeof(chn->mix));
    Unlock();
}

void ndspChnGetMix(int id, float mix[12])
{
    Channel *chn = Get(id);

    if (chn == NULL)
        return;
    Lock();
    memcpy(mix, chn->mix, sizeof(chn->mix));
    Unlock();
}

void ndspChnWaveBufClear(int id)
{
    Channel *chn = Get(id);

    if (chn == NULL)
        return;
    Lock();
    DropQueue(chn);
    chn->seqNext = 0;
    Unlock();
}

void ndspChnWaveBufAdd(int id, ndspWaveBuf *buf)
{
    Channel *chn = Get(id);
    ndspWaveBuf **link;
    u16 seq;

    if (chn == NULL || buf == NULL || buf->nsamples == 0)
        return;
    Lock();
    bool muted = RefreshSpeedPolicy();
    if (buf->status == NDSP_WBUF_QUEUED || buf->status == NDSP_WBUF_PLAYING)
    {
        Unlock();
        return;
    }
    buf->next = NULL;
    seq = chn->seqNext ? chn->seqNext : 1;
    buf->sequence_id = seq;
    chn->seqNext = seq + 1;
    if (muted)
    {
        /* Complete immediately: the producer's bounded ring remains usable
         * even when several game ticks run between audio callbacks. */
        SetStatus(buf, NDSP_WBUF_DONE);
        Unlock();
        return;
    }
    SetStatus(buf, NDSP_WBUF_QUEUED);
    for (link = &chn->head; *link != NULL; link = &(*link)->next)
        ;
    *link = buf;
    /* From silence the first output frame fades in from zero to the first
     * sample rather than starting on it with a click. */
    if (!chn->active)
    {
        chn->active = true;
        chn->frac = 1.0;
        chn->aL = chn->aR = chn->bL = chn->bR = 0.0f;
    }
    Unlock();
}

Result DSP_FlushDataCache(const void *address, u32 size)
{
    (void)address;
    (void)size;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    return 0;
}

Result DSP_InvalidateDataCache(const void *address, u32 size)
{
    (void)address;
    (void)size;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return 0;
}
