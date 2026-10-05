#include "gpu_internal.h"
#include <ctr_gpu_voxel.h>

/* This budget includes both high-resolution color/depth targets, the final
 * LCD texture, and the worst supported 4-sample terrain color/depth buffer.
 * Native textures/CPU buffers remain at their original dimensions. */
#define VOXEL_EXTRA_BUDGET (128u * 1024u * 1024u)
static bool initialized;
static unsigned maximum=1;
static GLint maxTexture,maxRenderbuffer,maxViewport[2];
#ifdef CTR_GPU_TEST
static unsigned failedScales,failedStage,allocations;
void gpuTestVoxelScaleFailure(unsigned scales,unsigned stage)
{ failedScales=scales; failedStage=stage; }
unsigned gpuTestVoxelScaleAllocations(void) { return allocations; }
#endif

bool gpuScaleAllocationFails(unsigned scale,unsigned stage)
{
#ifdef CTR_GPU_TEST
    return (failedScales&(1u<<scale)) && (!failedStage || failedStage==stage);
#else
    (void)scale; (void)stage; return false;
#endif
}

static bool fits(unsigned width,unsigned height,unsigned scale)
{
    return (size_t)width*scale<=(unsigned)maxTexture && (size_t)height*scale<=(unsigned)maxTexture
        && (size_t)width*scale<=(unsigned)maxRenderbuffer && (size_t)height*scale<=(unsigned)maxRenderbuffer
        && (size_t)width*scale<=(unsigned)maxViewport[0] && (size_t)height*scale<=(unsigned)maxViewport[1];
}

static uint64_t extraBytes(C3D_RenderTarget *logical,C3D_RenderTarget *top,unsigned scale)
{
    if(scale<=1) return 0;
    /* GLES renderbuffer depth is D24 with four-byte accounting, irrespective
     * of the public 3DS depth format. Reserve MSAA even when currently Off. */
    uint64_t scene=(uint64_t)logical->frameBuf.width*logical->frameBuf.height;
    uint64_t screen=(uint64_t)top->frameBuf.width*top->frameBuf.height;
    return (scene*(8+4*8)+screen*8+240*400*4)*scale*scale;
}
#ifdef CTR_GPU_TEST
uint64_t gpuTestVoxelScaleBytes(C3D_RenderTarget *logical,C3D_RenderTarget *top,unsigned scale)
{ return extraBytes(logical,top,scale); }
#endif

void gpuVoxelScaleInit(void)
{
    if(initialized) return;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxTexture);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&maxRenderbuffer);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS,maxViewport);
    maximum=1;
    for(unsigned scale=2;scale<=4;scale++)
        if(fits(512,256,scale) && fits(240,400,scale)) maximum=scale;
    initialized=true;
    CtrHost_SetVoxelScaleCapabilities((int)maximum);
}

void gpuVoxelScaleLimit(unsigned limit)
{
    if(limit<1) limit=1;
    if(limit<maximum) { maximum=limit; CtrHost_SetVoxelScaleCapabilities((int)maximum); }
}

void gpuReleaseTargetScale(GpuTarget *target)
{
    if(!target) return;
    target->fbo=target->nativeFbo; target->color=target->nativeColor; target->depth=target->nativeDepth;
    target->scale=1;
    glDeleteFramebuffers(1,&target->scaledFbo); glDeleteTextures(1,&target->scaledColor);
    glDeleteRenderbuffers(1,&target->scaledDepth);
    target->scaledFbo=target->scaledColor=target->scaledDepth=target->cachedScale=0;
}

void gpuVoxelScaleShutdown(void)
{
    for(GpuTarget *target=gpuTargets;target;target=target->next) gpuReleaseTargetScale(target);
    gpuReleaseScreenScales();
    initialized=false; maximum=1;
    CtrHost_SetVoxelScaleCapabilities(-1);
}

static bool allocationOkay(void)
{
    bool okay=true;
    for(GLenum error;(error=glGetError())!=GL_NO_ERROR;) {
        GPU_LOG("voxel resolution allocation failed: 0x%x",error); okay=false;
    }
    return okay;
}

static bool selectTarget(GpuTarget *record,unsigned scale,unsigned stage)
{
    if(!record) return false;
    if(scale<=1) {
        record->fbo=record->nativeFbo; record->color=record->nativeColor; record->depth=record->nativeDepth;
        record->scale=1; return true;
    }
    C3D_FrameBuf *buffer=&record->target->frameBuf;
    if(buffer->colorFmt!=GPU_RB_RGBA8 || !fits(buffer->width,buffer->height,scale)
       || gpuScaleAllocationFails(scale,stage)) return false;
    if(record->cachedScale!=scale) {
        /* Only one larger backing is retained per target. Release it before
         * changing sizes, keeping allocation peaks within the fixed budget. */
        gpuReleaseTargetScale(record);
        unsigned width=buffer->width*scale,height=buffer->height*scale;
        glGenTextures(1,&record->scaledColor); glBindTexture(GL_TEXTURE_2D,record->scaledColor);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1,&record->scaledFbo); glBindFramebuffer(GL_FRAMEBUFFER,record->scaledFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,record->scaledColor,0);
        if(record->nativeDepth) {
            glGenRenderbuffers(1,&record->scaledDepth); glBindRenderbuffer(GL_RENDERBUFFER,record->scaledDepth);
            glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,record->scaledDepth);
        }
        bool complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        if(!allocationOkay() || !complete) { gpuReleaseTargetScale(record); return false; }
        record->cachedScale=scale;
#ifdef CTR_GPU_TEST
        allocations++;
#endif
    }
    record->fbo=record->scaledFbo; record->color=record->scaledColor; record->depth=record->scaledDepth;
    record->scale=scale; return true;
}

unsigned CtrGpu_ConfigureVoxelTargets(C3D_RenderTarget *logical,C3D_RenderTarget *top,
                                     bool voxel,bool keepTop)
{
    gpuC2DFlush();
    GpuTarget *scene=logical?gpuFindTarget(&logical->frameBuf):NULL;
    GpuTarget *screen=top?gpuFindTarget(&top->frameBuf):NULL;
    if(!scene || !screen) return 1;
    CtrHostLayout layout; CtrHost_GetLayout(&layout);
    unsigned scale=voxel && !keepTop && layout.voxelScale>=1 && layout.voxelScale<=4
        ?(unsigned)layout.voxelScale:1;
    if(scale>maximum) scale=maximum;
    GLint oldRenderbuffer; glGetIntegerv(GL_RENDERBUFFER_BINDING,&oldRenderbuffer);
    if((GLuint)oldRenderbuffer==scene->scaledDepth || (GLuint)oldRenderbuffer==screen->scaledDepth)
        oldRenderbuffer=0;
    for(;scale>1;scale--) {
        bool okay=extraBytes(logical,top,scale)<=VOXEL_EXTRA_BUDGET
            && selectTarget(scene,scale,1) && selectTarget(screen,scale,2)
            && gpuScreenScale(GFX_TOP,scale);
        if(okay) break;
        gpuReleaseTargetScale(scene); gpuReleaseTargetScale(screen);
        gpuVoxelAaReleaseSurface();
        gpuScreenScale(GFX_TOP,1); gpuReleaseScreenScales();
        gpuVoxelScaleLimit(scale-1);
    }
    if(scale==1) { selectTarget(scene,1,1); if(!keepTop) selectTarget(screen,1,2); }
    glBindRenderbuffer(GL_RENDERBUFFER,(GLuint)oldRenderbuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTarget?gpuTarget->fbo:0);
    if(gpuTarget) glViewport(0,0,gpuTarget->target->frameBuf.width*gpuTarget->scale,
                                  gpuTarget->target->frameBuf.height*gpuTarget->scale);
    gpuApplyState();
    return scale;
}
