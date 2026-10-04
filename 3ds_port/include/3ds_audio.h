#ifndef CTR_AUDIO_H
#define CTR_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/*
 * One mixer frame is 701 stereo samples produced 60 times a second: m4a's
 * portable path fixes pcmSamplesPerVBlank at 701 and derives pcmFreq as
 * 60.0f * that. The bridge checks the game agrees before the first frame is
 * queued rather than trusting this constant.
 */
#define CTR_AUDIO_SAMPLE_RATE 42060

/* Mixer frames we try to keep queued ahead of the DSP: 50 ms of slack. */
#define CTR_AUDIO_TARGET_DEPTH 3

typedef struct
{
    uint32_t queued;     /* wave buffers the DSP has still to play */
    uint32_t frames;     /* mixer frames handed to NDSP */
    uint32_t underruns;  /* frames that arrived after the DSP had run dry */
    uint32_t drops;      /* frames discarded because every buffer was busy */
    uint32_t peak;       /* loudest sample of the last frame, 0..32767 */
    uint32_t voices;     /* sample channels the mixer had active */
    uint32_t song;       /* song number the map music player last asked for */
    float rateHz;        /* playback rate after drift correction */
    float submitMs;      /* backend cost of the last frame */
    float mixMs;         /* mixer cost of the last frame */
} CtrAudioStats;

bool CtrAudio_Init(void);
void CtrAudio_Shutdown(void);
void CtrAudio_Queue(const float *interleaved, int frames);
CtrAudioStats *CtrAudio_Stats(void);
bool CtrAudio_Available(void);

#endif
