#ifndef CTR_DIAGNOSTICS_H
#define CTR_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

#define CTR_DIAGNOSTIC_FRAME_CAPACITY 240
#define CTR_DIAGNOSTIC_ERROR_CAPACITY 16

enum CtrDiagnosticError { CTR_DIAG_EGL_INIT = 1, CTR_DIAG_EGL_SURFACE, CTR_DIAG_EGL_CURRENT, CTR_DIAG_EGL_SWAP };
typedef struct { uint64_t atNs, workNs, intervalNs; unsigned surfaces; } CtrDiagnosticFrame;
typedef struct { uint64_t atNs; int site, code; } CtrDiagnosticError;
typedef struct {
    bool recording, graphicsReady;
    char vendor[128], renderer[256], version[128];
    uint64_t totalFrames;
    unsigned frameCount, errorCount;
    CtrDiagnosticFrame frames[CTR_DIAGNOSTIC_FRAME_CAPACITY];
    CtrDiagnosticError errors[CTR_DIAGNOSTIC_ERROR_CAPACITY];
} CtrDiagnosticSnapshot;

void CtrDiagnostics_SetRecording(bool enabled);
/* Odd epochs record; an epoch change discards an in-flight old sample. */
unsigned CtrDiagnostics_Epoch(void);
uint64_t CtrDiagnostics_NowNs(void);
void CtrDiagnostics_Graphics(const char *vendor, const char *renderer, const char *version);
void CtrDiagnostics_ResetClock(void);
void CtrDiagnostics_Present(unsigned epoch, uint64_t startNs, uint64_t endNs, unsigned surfaces);
void CtrDiagnostics_Error(int site, int code);
void CtrDiagnostics_Snapshot(CtrDiagnosticSnapshot *out);

#endif
