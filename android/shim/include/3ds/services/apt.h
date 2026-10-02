/**
 * @file apt.h
 * @brief libctru's applet lifecycle (zlib licence, devkitPro), driven by the
 * Android activity (ctr_host.h).
 *
 * aptMainLoop() is where the game thread notices the activity's state:
 * running returns true; paused calls the APTHOOK_ONSUSPEND hooks, blocks
 * until the activity comes back, calls the APTHOOK_ONRESTORE hooks and
 * returns true; finishing calls the APTHOOK_ONEXIT hooks and returns false
 * (and keeps returning false). The shim's own components (audio, GPU) follow
 * the same transitions through ctrshim_apt.h.
 */
#pragma once

#include <3ds/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    APTHOOK_ONSUSPEND = 0,
    APTHOOK_ONRESTORE,
    APTHOOK_ONSLEEP,
    APTHOOK_ONWAKEUP,
    APTHOOK_ONEXIT,

    APTHOOK_COUNT,
} APT_HookType;

typedef void (*aptHookFn)(APT_HookType hook, void *param);

typedef struct tag_aptHookCookie
{
    struct tag_aptHookCookie *next;
    aptHookFn callback;
    void *param;
} aptHookCookie;

Result aptInit(void);
void aptExit(void);
bool aptIsActive(void);
bool aptShouldClose(void);
void aptSetSleepAllowed(bool allowed);
bool aptIsSleepAllowed(void);
bool aptMainLoop(void);
/* Hooks run newest first, on the thread calling aptMainLoop. */
void aptHook(aptHookCookie *cookie, aptHookFn callback, void *param);
void aptUnhook(aptHookCookie *cookie);
/* Reports a New 3DS: what a phone's CPU and memory compare to. */
Result APT_CheckNew3DS(bool *out);

#ifdef __cplusplus
}
#endif
