/**
 * @file ndsp.h
 * @brief libctru's NDSP (zlib licence, devkitPro), on AAudio.
 *
 * 24 channels are mixed in the AAudio data callback straight to the device's
 * native rate, each resampled from its own rate (ndspChnSetRate, which may
 * change at any time) with linear interpolation. Wave buffers move
 * FREE/DONE -> QUEUED (ndspChnWaveBufAdd) -> PLAYING (first sample consumed)
 * -> DONE (last sample consumed), written by the callback thread; status is
 * volatile and stored with release semantics, so polling it from the game
 * thread is safe, as on the console. Output pauses while the activity is
 * paused (aptMainLoop) and the stream is reopened if the device disconnects.
 *
 * ndspInit fails, like a console without DSP firmware, if no output stream
 * can be opened. ADPCM, capture, aux buses and filters are not implemented.
 */
#pragma once

#include <3ds/types.h>
#include <3ds/os.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NDSP_SAMPLE_RATE (SYSCLOCK_SOC / 512.0)

typedef enum
{
    NDSP_OUTPUT_MONO = 0,
    NDSP_OUTPUT_STEREO = 1,
    NDSP_OUTPUT_SURROUND = 2,
} ndspOutputMode;

typedef enum
{
    NDSP_CLIP_NORMAL = 0,
    NDSP_CLIP_SOFT = 1,
} ndspClippingMode;

typedef struct
{
    u16 index;
    s16 history0;
    s16 history1;
} ndspAdpcmData;

typedef struct tag_ndspWaveBuf ndspWaveBuf;

enum
{
    NDSP_WBUF_FREE = 0,
    NDSP_WBUF_QUEUED = 1,
    NDSP_WBUF_PLAYING = 2,
    NDSP_WBUF_DONE = 3,
};

struct tag_ndspWaveBuf
{
    union
    {
        s8 *data_pcm8;
        s16 *data_pcm16;
        u8 *data_adpcm;
        const void *data_vaddr;
    };
    /* Sample frames (a stereo PCM16 frame is two halfwords). */
    u32 nsamples;
    ndspAdpcmData *adpcm_data;
    u32 offset;
    bool looping;
    /* Written by the audio thread. */
    volatile u8 status;
    u16 sequence_id;
    ndspWaveBuf *next;
};

typedef void (*ndspCallback)(void *data);

Result ndspInit(void);
void ndspExit(void);
u32 ndspGetDroppedFrames(void);
u32 ndspGetFrameCount(void);
void ndspSetMasterVol(float volume);
float ndspGetMasterVol(void);
void ndspSetOutputMode(ndspOutputMode mode);
ndspOutputMode ndspGetOutputMode(void);
void ndspSetClippingMode(ndspClippingMode mode);
ndspClippingMode ndspGetClippingMode(void);
void ndspSetOutputCount(int count);
int ndspGetOutputCount(void);
/* Called on the audio thread after each DSP frame's worth of output (160 samples). */
void ndspSetCallback(ndspCallback callback, void *data);

#ifdef __cplusplus
}
#endif
