#pragma once
#include <citro2d.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <ctr_host.h>
#include <ctrshim_mem.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#define GPU_LOG(...) __android_log_print(ANDROID_LOG_ERROR,"EmeraldGPU",__VA_ARGS__)

typedef struct GpuTexture {
    struct GpuTexture *next;
    C3D_Tex *tex;
    GLuint id;
    unsigned char *shadow, *rgba;
    void *data;
    unsigned width, height;
    size_t size;
    GPU_TEXCOLOR format;
    bool authoritative, uploaded, ownsData;
} GpuTexture;
typedef struct GpuTarget {
    struct GpuTarget *next;
    C3D_RenderTarget *target;
    GLuint fbo, color, depth;
    GpuTexture *texture;
} GpuTarget;
extern GpuTexture *gpuTextures;
extern GpuTarget *gpuTargets, *gpuTarget;
extern C3D_TexEnv gpuEnvs[6];
extern C3D_AttrInfo gpuAttrs;
extern C3D_BufInfo gpuBuffers;
extern shaderProgram_s *gpuProgram;
extern bool gpuC2DProgram;
extern bool gpuAlphaEnabled;
extern GPU_TESTFUNC gpuAlphaFunc;
extern int gpuAlphaRef;
extern unsigned char __C3D_Context[];
#define GPU_BOUND_TEXTURES ((C3D_Tex **)(void *)(__C3D_Context+0x118))

bool gpuInit(void);
void gpuShutdown(void);
void gpuPresent(void);
void gpuPace(void);
bool gpuShouldRender(void);
void gpuApplyState(void);
void gpuFlushScreens(void);
void gpuTransferToScreen(GpuTarget *target,gfxScreen_t screen,unsigned width,unsigned height);
GpuTarget *gpuFindTarget(C3D_FrameBuf *buffer);
GpuTexture *gpuFindTexture(C3D_Tex *texture);
GLuint gpuTextureId(C3D_Tex *texture);
GLuint gpuCompile(GLenum type,const char *source);
GLuint gpuLink(GLuint vertex,GLuint fragment);
enum { GPU_PROGRAM_TRANSLATED, GPU_PROGRAM_C2D, GPU_PROGRAM_C2D_RAW };
GLuint gpuUseProgram(unsigned mode);
bool gpuC2DTransform(float out[12]);
void gpuSetTextureParams(C3D_Tex *texture);
void gpuC2DFlush(void);
void gpuC2DResetFrame(void);
void gpuVoxelAaInit(void);
void gpuVoxelAaShutdown(void);
double gpuNow(void);
size_t gpuTextureSize(unsigned width,unsigned height,GPU_TEXCOLOR format);
bool gpuDecodeTexture(const void *source,unsigned char *rgba,unsigned width,unsigned height,GPU_TEXCOLOR format);
void gpuDecodePixel(const unsigned char *source,GPU_TEXCOLOR format,unsigned char *rgba);
unsigned gpuPixelBytes(GPU_TEXCOLOR format);
