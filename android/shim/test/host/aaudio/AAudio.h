/* Platform declarations only; production ndsp_aaudio.c is tested unchanged. */
#pragma once
#include <stdint.h>
typedef int32_t aaudio_result_t;
typedef int32_t aaudio_data_callback_result_t;
typedef struct AAudioStream AAudioStream;
typedef struct AAudioStreamBuilder AAudioStreamBuilder;
typedef aaudio_data_callback_result_t (*AAudioStream_dataCallback)(AAudioStream *, void *, void *, int32_t);
typedef void (*AAudioStream_errorCallback)(AAudioStream *, void *, aaudio_result_t);
enum { AAUDIO_OK = 0, AAUDIO_ERROR_UNAVAILABLE = -889, AAUDIO_ERROR_DISCONNECTED = -899,
       AAUDIO_CALLBACK_RESULT_CONTINUE = 0, AAUDIO_DIRECTION_OUTPUT = 0,
       AAUDIO_SHARING_MODE_SHARED = 1, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY = 12,
       AAUDIO_FORMAT_PCM_FLOAT = 2, AAUDIO_USAGE_GAME = 14, AAUDIO_CONTENT_TYPE_MUSIC = 2 };
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **);
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *);
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setUsage(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setContentType(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *, AAudioStream_dataCallback, void *);
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *, AAudioStream_errorCallback, void *);
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *, AAudioStream **);
int32_t AAudioStream_getFormat(AAudioStream *);
int32_t AAudioStream_getChannelCount(AAudioStream *);
int32_t AAudioStream_getFramesPerBurst(AAudioStream *);
int32_t AAudioStream_getSampleRate(AAudioStream *);
int32_t AAudioStream_getPerformanceMode(AAudioStream *);
int32_t AAudioStream_setBufferSizeInFrames(AAudioStream *, int32_t);
aaudio_result_t AAudioStream_requestStart(AAudioStream *);
aaudio_result_t AAudioStream_requestStop(AAudioStream *);
aaudio_result_t AAudioStream_requestPause(AAudioStream *);
aaudio_result_t AAudioStream_close(AAudioStream *);
const char *AAudio_convertResultToText(aaudio_result_t);
