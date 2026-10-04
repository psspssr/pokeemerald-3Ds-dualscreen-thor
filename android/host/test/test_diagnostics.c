#include "ctr_diagnostics.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static void *Produce(void *unused)
{
    (void)unused;
    for (unsigned i = 1; i <= 12000; i++) {
        unsigned epoch = CtrDiagnostics_Epoch();
        CtrDiagnostics_Present(epoch, i * 1000u, i * 1000u + 10u, 2);
        if (!(i % 127)) CtrDiagnostics_Error(CTR_DIAG_EGL_SWAP, (int)i);
    }
    return NULL;
}

static void *Toggle(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 1000; i++) {
        CtrDiagnostics_SetRecording(false);
        CtrDiagnostics_SetRecording(true);
    }
    return NULL;
}

int main(void)
{
    CtrDiagnosticSnapshot snapshot;
    CtrDiagnostics_Snapshot(&snapshot);
    assert(!snapshot.recording && !snapshot.frameCount && !(CtrDiagnostics_Epoch() & 1));
    CtrDiagnostics_Present(CtrDiagnostics_Epoch(), 1000, 2000, 1);
    CtrDiagnostics_Error(CTR_DIAG_EGL_INIT, 1);
    CtrDiagnostics_Snapshot(&snapshot);
    assert(!snapshot.frameCount && !snapshot.errorCount);

    char vendor[700];
    memset(vendor, 'V', sizeof(vendor)); vendor[sizeof(vendor) - 1] = 0;
    CtrDiagnostics_Graphics(vendor, "actual driver", "OpenGL ES 3.0");
    vendor[0] = 'X';
    CtrDiagnostics_SetRecording(true);
    unsigned epoch = CtrDiagnostics_Epoch();
    CtrDiagnostics_Present(epoch, 1000, 2000, 1);
    CtrDiagnostics_Present(epoch, 2500, 4000, 2);
    CtrDiagnostics_SetRecording(true); /* Reconfiguration must not discard history. */
    CtrDiagnostics_Snapshot(&snapshot);
    assert(snapshot.graphicsReady && strlen(snapshot.vendor) == sizeof(snapshot.vendor) - 1 && snapshot.vendor[0] == 'V');
    assert(snapshot.frameCount == 2 && snapshot.totalFrames == 2);
    assert(snapshot.frames[0].workNs == 1000 && snapshot.frames[0].intervalNs == 0);
    assert(snapshot.frames[1].workNs == 1500 && snapshot.frames[1].intervalNs == 2000 && snapshot.frames[1].surfaces == 2);
    CtrDiagnostics_ResetClock();
    CtrDiagnostics_Present(epoch, 1000000, 1000010, 1);
    CtrDiagnostics_Snapshot(&snapshot);
    assert(snapshot.frames[2].intervalNs == 0); /* A pause is not a slow frame. */

    CtrDiagnostics_SetRecording(false);
    CtrDiagnostics_SetRecording(true);
    CtrDiagnostics_Present(epoch, 1, 10, 1); /* Old in-flight recording session. */
    epoch = CtrDiagnostics_Epoch();
    CtrDiagnostics_Present(epoch, 1, 10, 0); /* No successful window swap. */
    CtrDiagnostics_Present(epoch, 10, 1, 1);
    CtrDiagnostics_Present(epoch, 1, 10, 3);
    CtrDiagnostics_Snapshot(&snapshot);
    assert(snapshot.frameCount == 0);
    for (unsigned i = 0; i < CTR_DIAGNOSTIC_FRAME_CAPACITY + 20; i++)
        CtrDiagnostics_Present(epoch, 1000 + i * 100, 1010 + i * 100, 2);
    for (unsigned i = 0; i < CTR_DIAGNOSTIC_ERROR_CAPACITY + 10; i++)
        CtrDiagnostics_Error(CTR_DIAG_EGL_SWAP, (int)i);
    CtrDiagnostics_Snapshot(&snapshot);
    assert(snapshot.frameCount == CTR_DIAGNOSTIC_FRAME_CAPACITY && snapshot.totalFrames == CTR_DIAGNOSTIC_FRAME_CAPACITY + 20);
    assert(snapshot.frames[0].atNs == 3010 && snapshot.frames[snapshot.frameCount - 1].atNs == 26910);
    assert(snapshot.errorCount == CTR_DIAGNOSTIC_ERROR_CAPACITY && snapshot.errors[0].code == 10);

    pthread_t producer, toggle;
    assert(!pthread_create(&producer, NULL, Produce, NULL));
    assert(!pthread_create(&toggle, NULL, Toggle, NULL));
    for (unsigned i = 0; i < 6000; i++) {
        CtrDiagnostics_Snapshot(&snapshot);
        assert(snapshot.frameCount <= CTR_DIAGNOSTIC_FRAME_CAPACITY && snapshot.errorCount <= CTR_DIAGNOSTIC_ERROR_CAPACITY);
        if (!snapshot.recording) assert(!snapshot.frameCount && !snapshot.errorCount && !snapshot.totalFrames);
        for (unsigned j = 0; j < snapshot.frameCount; j++) assert(snapshot.frames[j].surfaces == 2);
    }
    assert(!pthread_join(producer, NULL)); assert(!pthread_join(toggle, NULL));
    CtrDiagnostics_SetRecording(false);
    CtrDiagnostics_Snapshot(&snapshot);
    assert(!snapshot.frameCount && !snapshot.errorCount && !snapshot.totalFrames && snapshot.graphicsReady);
    puts("diagnostics: opt-in/reset, bounded identity/rings, dual-surface counting, pause gaps, stale epochs and concurrent snapshots passed");
    return 0;
}
