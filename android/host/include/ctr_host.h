#ifndef CTR_HOST_H
#define CTR_HOST_H

/*
 * The contract between the Android side (JNI, android/host/src) and the
 * native shim (android/shim, android/gpu). Writers: the JNI bridge, from the
 * UI thread. Readers: the shim, from the game thread. Every function here is
 * thread safe.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ANativeWindow;

typedef struct
{
    int x, y, w, h;
} CtrHostRect;

typedef struct
{
    /* libctru KEY_* bits (3ds/services/hid.h), KEY_TOUCH included while touching. */
    uint32_t keys;
    /* Circle pad, libctru's range (about -156..156, up is positive). */
    int16_t circleX, circleY;
    /* Bottom-screen pixel coordinates, 0..319 and 0..239, valid while touching. */
    uint16_t touchX, touchY;
} CtrHostInput;

typedef enum
{
    CTR_HOST_RUNNING,
    /* The activity is not visible: the game thread blocks in aptMainLoop. */
    CTR_HOST_PAUSED,
    /* The activity is finishing: aptMainLoop returns false. */
    CTR_HOST_EXITING,
} CtrHostState;

/*
 * Window 0 is the activity's own surface on the main display. Window 1 is a
 * surface on a second physical display (the AYN Thor's bottom screen, see
 * docs/AYN_THOR.md), present only in dual-display mode.
 */
#define CTR_HOST_MAX_WINDOWS 2

typedef struct
{
    /* Where each screen goes in its window, in that window's pixels. w == 0 hides it. */
    CtrHostRect top, bottom;
    /* 0 nearest, 1 linear. */
    int filter;
    /* Window clear colour, 0xRRGGBB. */
    uint32_t background;
    /* Which window (0 or 1) each screen is drawn into. Zero-initialised:
     * both screens share window 0. */
    int topWindow, bottomWindow;
} CtrHostLayout;

/* ── Written by the JNI bridge ──────────────────────────────────────────── */
void CtrHost_SetPaths(const char *romfsDir, const char *sdmcDir);
/* Window 0. */
void CtrHost_SetWindow(struct ANativeWindow *window);
/* Window index 0..CTR_HOST_MAX_WINDOWS-1; NULL removes it. */
void CtrHost_SetWindowAt(int index, struct ANativeWindow *window);
void CtrHost_SetLayout(const CtrHostLayout *layout);
void CtrHost_SetInput(const CtrHostInput *input);
void CtrHost_SetState(CtrHostState state);
/* Optional gameplay rules are never stored in the GBA save. Speed is the
 * currently active multiplier, not the configured shortcut's target speed. */
void CtrHost_SetGameplayOptions(unsigned speed, unsigned shinyMultiplier,
                               bool sharedExperience, bool saveBackups, bool protectShinies);
unsigned CtrHost_GameSpeed(void);
unsigned CtrHost_ShinyMultiplier(void);
bool CtrHost_SharedExperience(void);
bool CtrHost_SaveBackups(void);
bool CtrHost_ProtectShinies(void);
/* Game thread only. Pause/exit, missing UI and dismissal all mean Stay. */
bool CtrHost_ConfirmShinyFlee(void);
void CtrHost_AnswerShinyFlee(uint32_t request, bool allow);
bool CtrHost_IsShinyFleePending(uint32_t request);
/* Settings worker only. Requests run on the paused game thread, without
 * advancing frames. A queued request expires before it can mutate the game;
 * once a bounded in-memory operation begins, its actual result is returned.
 * An empty snapshot means no active game/transport unavailable. */
unsigned CtrHost_QueryMysteryEvents(int *states, unsigned capacity, int timeoutMs);
int CtrHost_ActivateMysteryEvent(unsigned eventId, int timeoutMs);
/* Starts the game thread (origin's main()). Called once. */
bool CtrHost_StartGame(void);

/* ── Read by the shim, on the game thread ───────────────────────────────── */
/* "romfs:/" and "sdmc:/" map to these directories (no trailing slash). */
const char *CtrHost_RomfsDir(void);
const char *CtrHost_SdmcDir(void);
void CtrHost_GetInput(CtrHostInput *out);
void CtrHost_GetLayout(CtrHostLayout *out);
CtrHostState CtrHost_GetState(void);
/* Blocks while paused. Returns the state it leaves in (RUNNING or EXITING). */
CtrHostState CtrHost_WaitWhilePaused(void);
/* For storage workers: waits until the game has reached its pause point,
 * after its current frame and synchronous save writes. False on timeout or
 * while running/exiting; true before the game has been started. */
bool CtrHost_WaitUntilPaused(int timeoutMs);
/*
 * The current window with a reference held (ANativeWindow_acquire), or NULL.
 * *generation changes whenever the window is replaced or removed, so the GPU
 * side knows to recreate its EGL surface. Release with ANativeWindow_release.
 * After a generation change, destroy the previous EGL surface and release
 * its window before calling AcquireWindow: this acknowledges that release
 * to surfaceDestroyed. WindowGeneration only observes and never acknowledges.
 */
struct ANativeWindow *CtrHost_AcquireWindow(uint32_t *generation);
uint32_t CtrHost_WindowGeneration(void);
/* The same for any window; each has its own generation counter. The two
 * displays may refresh at different rates (Thor: 120 Hz top, 60 Hz bottom);
 * presentation never waits on one display's vsync for the other. */
struct ANativeWindow *CtrHost_AcquireWindowAt(int index, uint32_t *generation);
uint32_t CtrHost_WindowGenerationAt(int index);

/* ── Called by the shim, implemented by the JNI bridge ──────────────────── */
/* The game thread is ending (main returned or exit was called). */
void CtrHost_NotifyGameExit(int status);
/* A short rumble, for future use; may be a no-op. */
void CtrHost_Vibrate(int milliseconds);
/* JNI posts these to the UI thread. No gameplay state is changed by them. */
bool CtrHost_ShowShinyFleePrompt(uint32_t request);
void CtrHost_NotifyBackupFailure(void);

#ifdef __cplusplus
}
#endif

#endif
