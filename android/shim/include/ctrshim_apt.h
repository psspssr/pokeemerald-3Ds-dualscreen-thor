#ifndef CTRSHIM_APT_H
#define CTRSHIM_APT_H

/*
 * Lifecycle notifications for the shim's own components (NDSP, the GPU
 * emulation), so they follow the activity the way the game's APT hooks do.
 *
 * Listeners run on the game thread, from aptMainLoop:
 *   CTR_APT_SUSPEND  after the game's APTHOOK_ONSUSPEND hooks, just before
 *                    the game thread blocks while the activity is paused
 *                    (the window may be destroyed while it is blocked);
 *   CTR_APT_RESUME   when the activity is back, before the game's
 *                    APTHOOK_ONRESTORE hooks;
 *   CTR_APT_EXIT     after the game's APTHOOK_ONEXIT hooks, when
 *                    aptMainLoop is about to return false. A pause that ends
 *                    in EXITING sends SUSPEND then EXIT, without RESUME.
 * Listeners are called in registration order for RESUME and in reverse
 * order for SUSPEND and EXIT. They may not add or remove listeners.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    CTR_APT_SUSPEND,
    CTR_APT_RESUME,
    CTR_APT_EXIT,
} CtrAptEvent;

typedef void (*CtrAptListener)(CtrAptEvent event, void *user);

/* False when the (small, fixed) table is full or the pair is already there. */
bool CtrApt_AddListener(CtrAptListener listener, void *user);
void CtrApt_RemoveListener(CtrAptListener listener, void *user);
/* True between SUSPEND and RESUME (any thread may ask). */
bool CtrApt_IsSuspended(void);

#ifdef __cplusplus
}
#endif

#endif
