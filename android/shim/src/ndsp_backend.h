#ifndef NDSP_BACKEND_H
#define NDSP_BACKEND_H

/*
 * Between the NDSP emulation (ndsp.c: channels, wave buffer queues,
 * resampling, mixing) and the audio output (ndsp_aaudio.c). The split keeps
 * the queue and resampler testable on a desktop with a fake output.
 */

#include <stdbool.h>

/*
 * Implemented by ndsp.c, called by the output's audio thread: fills out with
 * frames interleaved stereo float frames at rate Hz. Never blocks for longer
 * than a game thread holds the NDSP lock (a few list operations).
 */
void CtrNdsp_Render(float *out, int frames, int rate);

typedef struct
{
    /* Opens and starts the output; false if there is no output to be had. */
    bool (*open)(void);
    /* Stops and closes it. Render is not called again once this returns. */
    void (*close)(void);
    /* The activity went away / came back (also used on exit). */
    void (*pause)(void);
    void (*resume)(void);
} CtrNdspBackend;

/* ndsp_aaudio.c on Android; a test supplies its own. */
const CtrNdspBackend *CtrNdsp_GetBackend(void);

#endif
