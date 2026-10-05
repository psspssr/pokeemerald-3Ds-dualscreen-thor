#include "gpu_internal.h"
#include <ctr_gpu_voxel.h>

/* One cached surface for the logical voxel target. Color and depth must have
 * identical formats/bounds on both sides of an ES3 multisample resolve. The
 * ordinary backend stores RGBA8 color and DEPTH_COMPONENT24 for this target. */
static struct {
    GLuint fbo, color, depth;
    int width, height, samples, capabilities;
    bool initialized;
    GpuTarget *active;
    GLint previousRead, previousDraw;
} voxelAa;

#ifdef CTR_GPU_TEST
static unsigned testFailedSamples, testAllocations;
unsigned gpuTestVoxelAaSamples(void) { return voxelAa.active ? (unsigned)voxelAa.samples : 0; }
unsigned gpuTestVoxelAaAllocations(void) { return testAllocations; }
GLuint gpuTestVoxelAaFramebuffer(void) { return voxelAa.fbo; }
void gpuTestVoxelAaFailSamples(unsigned mask) { testFailedSamples = mask; }
#endif

static void releaseSurface(void)
{
    glDeleteFramebuffers(1, &voxelAa.fbo);
    glDeleteRenderbuffers(1, &voxelAa.color);
    glDeleteRenderbuffers(1, &voxelAa.depth);
    voxelAa.fbo = voxelAa.color = voxelAa.depth = 0;
    voxelAa.width = voxelAa.height = voxelAa.samples = 0;
}

/* A failed larger resolution must release the older MSAA allocation before
 * retrying smaller targets, while retaining the discovered sample support. */
void gpuVoxelAaReleaseSurface(void)
{ if(!voxelAa.active) releaseSurface(); }

static unsigned sampleBit(int samples) { return samples == 2 ? 1u : samples == 4 ? 2u : 0; }

static unsigned formatSamples(GLenum format)
{
    GLint count = 0, samples[64];
    unsigned mask = 0;
    glGetInternalformativ(GL_RENDERBUFFER, format, GL_NUM_SAMPLE_COUNTS, 1, &count);
    if (count <= 0 || count > (GLint)(sizeof(samples) / sizeof(samples[0]))) return 0;
    glGetInternalformativ(GL_RENDERBUFFER, format, GL_SAMPLES, count, samples);
    for (int i = 0; i < count; ++i) mask |= sampleBit(samples[i]);
    return mask;
}

void gpuVoxelAaInit(void)
{
    /* gpuInit and C3D_Init may both reach this in the same session. Keep an
     * allocation-failure fallback until a real renderer/context teardown. */
    if (voxelAa.initialized) return;
    voxelAa.capabilities = (int)(formatSamples(GL_RGBA8) & formatSamples(GL_DEPTH_COMPONENT24));
    voxelAa.initialized = true;
    CtrHost_SetVoxelAACapabilities(voxelAa.capabilities);
}

void gpuVoxelAaShutdown(void)
{
    voxelAa.active = NULL;
    releaseSurface();
    voxelAa.capabilities = 0;
    voxelAa.initialized = false;
    CtrHost_SetVoxelAACapabilities(-1);
}

static bool errorsClear(const char *step)
{
    bool okay = true;
    GLenum error;
    while ((error = glGetError()) != GL_NO_ERROR) {
        GPU_LOG("voxel MSAA %s failed: 0x%x", step, error);
        okay = false;
    }
    return okay;
}

static bool allocateSurface(int width, int height, int samples)
{
    GLint oldRenderbuffer, colorSamples = 0, depthSamples = 0;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &oldRenderbuffer);
    if ((GLuint)oldRenderbuffer == voxelAa.color || (GLuint)oldRenderbuffer == voxelAa.depth)
        oldRenderbuffer = 0;
    releaseSurface();
#ifdef CTR_GPU_TEST
    if (testFailedSamples & sampleBit(samples)) return false;
    ++testAllocations;
#endif
    glGenFramebuffers(1, &voxelAa.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, voxelAa.fbo);
    glGenRenderbuffers(1, &voxelAa.color);
    glBindRenderbuffer(GL_RENDERBUFFER, voxelAa.color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &colorSamples);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, voxelAa.color);
    glGenRenderbuffers(1, &voxelAa.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, voxelAa.depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &depthSamples);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, voxelAa.depth);
    bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    bool okay = errorsClear("allocation") && complete && colorSamples == samples && depthSamples == samples;
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)oldRenderbuffer);
    if (!okay) { releaseSurface(); return false; }
    voxelAa.width = width; voxelAa.height = height; voxelAa.samples = samples;
    return true;
}

static void restoreBindings(void)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)voxelAa.previousRead);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)voxelAa.previousDraw);
    gpuApplyState();
}

static void resolveTo(GpuTarget *target)
{
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, voxelAa.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->fbo);
    glBlitFramebuffer(0, 0, voxelAa.width, voxelAa.height,
                      0, 0, voxelAa.width, voxelAa.height,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
}

bool CtrGpu_BeginVoxelAA(C3D_RenderTarget *target)
{
    CtrHostLayout layout;
    CtrHost_GetLayout(&layout);
    int requested = layout.voxelAASamples;
    if ((requested != 2 && requested != 4) || voxelAa.active || !gpuTarget
        || gpuTarget->target != target || target->frameBuf.colorFmt != GPU_RB_RGBA8
        || !gpuTarget->depth) return false;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &voxelAa.previousRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &voxelAa.previousDraw);
    /* The game clears this logical target immediately before CtrVoxel_Draw;
     * no sky/background geometry precedes the terrain loop. glClear's color
     * and depth values still match that full clear, even after FrameDrawOn. */
    for (int samples = requested; samples >= 2; samples -= 2) {
        unsigned bit = sampleBit(samples);
        if (!(voxelAa.capabilities & (int)bit)) continue;
        unsigned width = target->frameBuf.width * gpuTarget->scale;
        unsigned height = target->frameBuf.height * gpuTarget->scale;
        bool fresh = voxelAa.width != (int)width || voxelAa.height != (int)height
            || voxelAa.samples != samples;
        bool okay = !fresh || allocateSurface((int)width, (int)height, samples);
        if (okay) {
            glBindFramebuffer(GL_FRAMEBUFFER, voxelAa.fbo);
            glDisable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (fresh) {
                /* Verify both resolve formats before drawing the first world.
                 * This only copies the same clear values to the original FBO,
                 * so a failed setup can still draw that frame without MSAA. */
                resolveTo(gpuTarget);
                okay = errorsClear("resolve validation");
            }
            if (okay) {
                voxelAa.active = gpuTarget;
                glBindFramebuffer(GL_FRAMEBUFFER, voxelAa.fbo);
                gpuApplyState();
                return true;
            }
        }
        releaseSurface();
        voxelAa.capabilities &= ~(int)bit;
        CtrHost_SetVoxelAACapabilities(voxelAa.capabilities);
        restoreBindings();
    }
    restoreBindings();
    return false;
}

void CtrGpu_EndVoxelAA(void)
{
    if (!voxelAa.active) return;
    resolveTo(voxelAa.active);
    if (!errorsClear("resolve")) {
        /* Future frames keep their ordinary single-sample path after a driver
         * failure. No preferences, game state or save data are changed. */
        voxelAa.capabilities = 0;
        CtrHost_SetVoxelAACapabilities(0);
    }
    voxelAa.active = NULL;
    restoreBindings();
}
