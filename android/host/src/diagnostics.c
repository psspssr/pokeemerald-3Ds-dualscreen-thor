#include "ctr_diagnostics.h"
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint sEpoch;
static CtrDiagnosticSnapshot sData;
static unsigned sFrameHead, sErrorHead;
static uint64_t sLastPresent;

unsigned CtrDiagnostics_Epoch(void) { return atomic_load_explicit(&sEpoch, memory_order_relaxed); }

uint64_t CtrDiagnostics_NowNs(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

void CtrDiagnostics_SetRecording(bool enabled)
{
    pthread_mutex_lock(&sLock);
    unsigned epoch = atomic_load_explicit(&sEpoch, memory_order_relaxed);
    if (((epoch & 1u) != 0) != enabled) {
        sData.recording = enabled;
        sData.totalFrames = sData.frameCount = sData.errorCount = 0;
        memset(sData.frames, 0, sizeof(sData.frames));
        memset(sData.errors, 0, sizeof(sData.errors));
        sFrameHead = sErrorHead = 0;
        sLastPresent = 0;
        atomic_store_explicit(&sEpoch, epoch + 1u, memory_order_relaxed);
    }
    pthread_mutex_unlock(&sLock);
}

static void Copy(char *out, size_t capacity, const char *value)
{
    if (!value) value = "";
    size_t length = strnlen(value, capacity - 1);
    memcpy(out, value, length);
    out[length] = '\0';
}

void CtrDiagnostics_Graphics(const char *vendor, const char *renderer, const char *version)
{
    pthread_mutex_lock(&sLock);
    Copy(sData.vendor, sizeof(sData.vendor), vendor);
    Copy(sData.renderer, sizeof(sData.renderer), renderer);
    Copy(sData.version, sizeof(sData.version), version);
    sData.graphicsReady = vendor && renderer && version;
    pthread_mutex_unlock(&sLock);
}

void CtrDiagnostics_ResetClock(void)
{
    pthread_mutex_lock(&sLock);
    sLastPresent = 0;
    pthread_mutex_unlock(&sLock);
}

void CtrDiagnostics_Present(unsigned epoch, uint64_t startNs, uint64_t endNs, unsigned surfaces)
{
    if (!(epoch & 1u) || !surfaces || surfaces > 2 || !startNs || endNs < startNs) return;
    pthread_mutex_lock(&sLock);
    if (epoch == atomic_load_explicit(&sEpoch, memory_order_relaxed)) {
        sData.frames[sFrameHead] = (CtrDiagnosticFrame){
            .atNs = endNs, .workNs = endNs - startNs,
            .intervalNs = sLastPresent && endNs > sLastPresent ? endNs - sLastPresent : 0,
            .surfaces = surfaces,
        };
        sLastPresent = endNs;
        sFrameHead = (sFrameHead + 1u) % CTR_DIAGNOSTIC_FRAME_CAPACITY;
        if (sData.frameCount < CTR_DIAGNOSTIC_FRAME_CAPACITY) sData.frameCount++;
        sData.totalFrames++;
    }
    pthread_mutex_unlock(&sLock);
}

void CtrDiagnostics_Error(int site, int code)
{
    unsigned epoch = CtrDiagnostics_Epoch();
    if (!(epoch & 1u)) return;
    uint64_t now = CtrDiagnostics_NowNs();
    pthread_mutex_lock(&sLock);
    if (epoch == atomic_load_explicit(&sEpoch, memory_order_relaxed)) {
        sData.errors[sErrorHead] = (CtrDiagnosticError){now, site, code};
        sErrorHead = (sErrorHead + 1u) % CTR_DIAGNOSTIC_ERROR_CAPACITY;
        if (sData.errorCount < CTR_DIAGNOSTIC_ERROR_CAPACITY) sData.errorCount++;
    }
    pthread_mutex_unlock(&sLock);
}

void CtrDiagnostics_Snapshot(CtrDiagnosticSnapshot *out)
{
    pthread_mutex_lock(&sLock);
    *out = sData;
    for (unsigned i = 0; i < sData.frameCount; i++)
        out->frames[i] = sData.frames[(sFrameHead + CTR_DIAGNOSTIC_FRAME_CAPACITY - sData.frameCount + i) % CTR_DIAGNOSTIC_FRAME_CAPACITY];
    for (unsigned i = 0; i < sData.errorCount; i++)
        out->errors[i] = sData.errors[(sErrorHead + CTR_DIAGNOSTIC_ERROR_CAPACITY - sData.errorCount + i) % CTR_DIAGNOSTIC_ERROR_CAPACITY];
    pthread_mutex_unlock(&sLock);
}
