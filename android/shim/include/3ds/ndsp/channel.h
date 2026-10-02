/**
 * @file channel.h
 * @brief libctru's NDSP channels (zlib licence, devkitPro). See ndsp.h.
 */
#pragma once

#include <3ds/types.h>
#include <3ds/ndsp/ndsp.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    NDSP_ENCODING_PCM8 = 0,
    NDSP_ENCODING_PCM16,
    NDSP_ENCODING_ADPCM,
};

#define NDSP_CHANNELS(n) ((u32)(n) & 3)
#define NDSP_ENCODING(n) (((u32)(n) & 3) << 2)

enum
{
    NDSP_FORMAT_MONO_PCM8 = NDSP_CHANNELS(1) | NDSP_ENCODING(NDSP_ENCODING_PCM8),
    NDSP_FORMAT_MONO_PCM16 = NDSP_CHANNELS(1) | NDSP_ENCODING(NDSP_ENCODING_PCM16),
    NDSP_FORMAT_MONO_ADPCM = NDSP_CHANNELS(1) | NDSP_ENCODING(NDSP_ENCODING_ADPCM),
    NDSP_FORMAT_STEREO_PCM8 = NDSP_CHANNELS(2) | NDSP_ENCODING(NDSP_ENCODING_PCM8),
    NDSP_FORMAT_STEREO_PCM16 = NDSP_CHANNELS(2) | NDSP_ENCODING(NDSP_ENCODING_PCM16),

    NDSP_FORMAT_PCM8 = NDSP_FORMAT_MONO_PCM8,
    NDSP_FORMAT_PCM16 = NDSP_FORMAT_MONO_PCM16,
    NDSP_FORMAT_ADPCM = NDSP_FORMAT_MONO_ADPCM,

    NDSP_FRONT_BYPASS = BIT(4),
    NDSP_3D_SURROUND_PREPROCESSED = BIT(6),
};

typedef enum
{
    NDSP_INTERP_POLYPHASE = 0,
    NDSP_INTERP_LINEAR = 1,
    NDSP_INTERP_NONE = 2,
} ndspInterpType;

void ndspChnReset(int id);
void ndspChnInitParams(int id);
bool ndspChnIsPlaying(int id);
u32 ndspChnGetSamplePos(int id);
u16 ndspChnGetWaveBufSeq(int id);
bool ndspChnIsPaused(int id);
void ndspChnSetPaused(int id, bool paused);
void ndspChnSetFormat(int id, u16 format);
u16 ndspChnGetFormat(int id);
/* Polyphase is played as linear. */
void ndspChnSetInterp(int id, ndspInterpType type);
ndspInterpType ndspChnGetInterp(int id);
/* In Hz. Takes effect from the next output sample. */
void ndspChnSetRate(int id, float rate);
float ndspChnGetRate(int id);
/* [0] front left, [1] front right, [2] back left, [3] back right; the aux buses ([4..11]) are not mixed. */
void ndspChnSetMix(int id, float mix[12]);
void ndspChnGetMix(int id, float mix[12]);
void ndspChnWaveBufClear(int id);
void ndspChnWaveBufAdd(int id, ndspWaveBuf *buf);

#ifdef __cplusplus
}
#endif
