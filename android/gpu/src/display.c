#include "gpu_internal.h"
#include "pacing.h"
#include <android/native_window.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>
#include <ctrshim_apt.h>
#include <ctr_diagnostics.h>
#include <ctr_bottom_content.h>

static EGLDisplay display=EGL_NO_DISPLAY;
static EGLContext context=EGL_NO_CONTEXT;
static EGLSurface pbuffer=EGL_NO_SURFACE;
static EGLConfig config;
static bool initialized,stereo;
static struct { EGLSurface surface; ANativeWindow *window; uint32_t generation; } windows[CTR_HOST_MAX_WINDOWS];
static struct {
    unsigned char *data,*shadow,*rgba;
    GSPGPU_FramebufferFormat format;
    GLuint texture,fbo;
    GLuint nativeTexture,nativeFbo,scaledTexture,scaledFbo;
    unsigned scale,cachedScale;
    unsigned columns;
    /* Origin's framebuffer writers flush complete LCD columns. A flush is a
     * CPU ownership claim even when its bytes equal the previous CPU shadow. */
    _Atomic u32 dirty[13];
} screens[2];
static GLuint presentProgram,presentVbo,presentVao;
static double nextVblank;
static double framePeriod=1.0/59.83;
static GpuFrameSchedule frameSchedule;
static bool listenerRegistered;
#ifdef CTR_GPU_TEST
static unsigned presentCount;
#endif
static void destroyWindow(int i);

static void lifecycle(CtrAptEvent event,void *user)
{
    (void)user;
    if(event==CTR_APT_SUSPEND || event==CTR_APT_EXIT) {
        gpuC2DFlush();
        for(int i=0;i<CTR_HOST_MAX_WINDOWS;i++) destroyWindow(i);
    }
    /* Anchor a resumed game's first frame at resume, not after its drawing.
     * Its FBOs remain intact; a long pause cannot leave stale deadlines. */
    nextVblank=event==CTR_APT_RESUME?gpuNow():0;
    frameSchedule=(GpuFrameSchedule){0};
    CtrDiagnostics_ResetClock();
}

double gpuNow(void)
{ struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now); return now.tv_sec+now.tv_nsec*1e-9; }
bool gpuShouldRender(void)
{
    unsigned speed=CtrHost_GameSpeed();
    if(speed<1 || speed>8) speed=1;
    if(frameSchedule.speed && frameSchedule.speed!=speed) nextVblank=gpuNow();
    return gpuFrameDue(&frameSchedule,speed);
}
void gpuPace(void)
{
    double now=gpuNow();
    nextVblank=gpuPacingDeadline(now,nextVblank,framePeriod);
    if(nextVblank<=now) return;
    struct timespec deadline={(time_t)nextVblank,(long)((nextVblank-(time_t)nextVblank)*1e9)};
    while(clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&deadline,NULL)==EINTR) {}
}
float C3D_FrameRate(float fps)
{ if(fps>0 && fps<=120) framePeriod=1.0/fps; return 1.0/framePeriod; }

static GPU_TEXCOLOR screenFormat(GSPGPU_FramebufferFormat format)
{
    static const GPU_TEXCOLOR formats[]={GPU_RGBA8,GPU_RGB8,GPU_RGB565,GPU_RGBA5551,GPU_RGBA4};
    return (unsigned)format<5?formats[format]:GPU_RGB565;
}

static void dirtyScreen(unsigned screen)
{
    for(unsigned word=0;word<13;word++)
        atomic_store_explicit(&screens[screen].dirty[word],UINT32_MAX,memory_order_release);
}

bool gpuInit(void)
{
    if(initialized) return true;
    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major,minor,count;
    if(display==EGL_NO_DISPLAY || !eglInitialize(display,&major,&minor)) goto fail;
    const EGLint attributes[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT|EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    if(!eglChooseConfig(display,attributes,&config,1,&count) || !count) goto fail;
    const EGLint ctxAttributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctxAttributes);
    const EGLint pbAttributes[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    pbuffer=eglCreatePbufferSurface(display,config,pbAttributes);
    if(context==EGL_NO_CONTEXT || pbuffer==EGL_NO_SURFACE || !eglMakeCurrent(display,pbuffer,pbuffer,context)) goto fail;
    const char *vs="#version 300 es\nlayout(location=0) in vec2 pos; layout(location=1) in vec2 uv; out vec2 tc; void main(){gl_Position=vec4(pos,0,1);tc=uv;}";
    const char *fs="#version 300 es\nprecision highp float;in vec2 tc;uniform sampler2D image;uniform vec4 sampleBounds;out vec4 color;void main(){color=vec4(texture(image,clamp(tc,sampleBounds.xy,sampleBounds.zw)).rgb,1);}";
    GLuint vertex=gpuCompile(GL_VERTEX_SHADER,vs),fragment=gpuCompile(GL_FRAGMENT_SHADER,fs);
    presentProgram=gpuLink(vertex,fragment); glDeleteShader(vertex); glDeleteShader(fragment);
    if(!presentProgram) goto fail;
    glGenVertexArrays(1,&presentVao); glGenBuffers(1,&presentVbo);
    for(int i=0;i<2;i++) {
        screens[i].columns=i?320:400; screens[i].format=GSP_BGR8_OES;
        size_t maxSize=(size_t)screens[i].columns*240*4;
        screens[i].data=calloc(1,maxSize); screens[i].shadow=malloc(maxSize); screens[i].rgba=calloc(1,maxSize);
        if(!screens[i].data || !screens[i].shadow || !screens[i].rgba) goto fail;
        memset(screens[i].shadow,0,maxSize);
        dirtyScreen(i);
        CtrMem_Register(CTR_MEM_FRAMEBUFFER,screens[i].data,maxSize,&screens[i]);
        glGenTextures(1,&screens[i].texture); glBindTexture(GL_TEXTURE_2D,screens[i].texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,240,screens[i].columns,0,GL_RGBA,GL_UNSIGNED_BYTE,screens[i].rgba);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1,&screens[i].fbo); glBindFramebuffer(GL_FRAMEBUFFER,screens[i].fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,screens[i].texture,0);
        screens[i].nativeTexture=screens[i].texture; screens[i].nativeFbo=screens[i].fbo; screens[i].scale=1;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0); glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    listenerRegistered=CtrApt_AddListener(lifecycle,NULL);
    if(!listenerRegistered) goto fail;
    initialized=true;
    gpuVoxelAaInit();
    gpuVoxelScaleInit();
    nextVblank=gpuNow();
    __android_log_print(ANDROID_LOG_INFO,"EmeraldGPU","GLES %s, %s",glGetString(GL_VERSION),glGetString(GL_RENDERER));
    CtrDiagnostics_Graphics((const char *)glGetString(GL_VENDOR),(const char *)glGetString(GL_RENDERER),(const char *)glGetString(GL_VERSION));
    return true;
fail: {
    EGLint error=eglGetError();
    GPU_LOG("EGL/GLES initialization failed: 0x%x",error);
    CtrDiagnostics_Error(CTR_DIAG_EGL_INIT,error);
    gpuShutdown(); return false;
}
}
static void destroyWindow(int i)
{
    if(windows[i].surface!=EGL_NO_SURFACE) {
        eglMakeCurrent(display,pbuffer,pbuffer,context);
        eglDestroySurface(display,windows[i].surface); windows[i].surface=EGL_NO_SURFACE;
    }
    if(windows[i].window) { ANativeWindow_release(windows[i].window); windows[i].window=NULL; }
}
static void refreshWindow(int i)
{
    uint32_t generation=CtrHost_WindowGenerationAt(i);
    if(generation==windows[i].generation && windows[i].surface!=EGL_NO_SURFACE) return;
    destroyWindow(i);
    windows[i].window=CtrHost_AcquireWindowAt(i,&windows[i].generation);
    if(!windows[i].window) return;
    EGLint format;
    eglGetConfigAttrib(display,config,EGL_NATIVE_VISUAL_ID,&format);
    ANativeWindow_setBuffersGeometry(windows[i].window,0,0,format);
    windows[i].surface=eglCreateWindowSurface(display,config,windows[i].window,NULL);
    if(windows[i].surface==EGL_NO_SURFACE) {
        EGLint error=eglGetError();
        GPU_LOG("window %d surface creation failed: 0x%x",i,error);
        CtrDiagnostics_Error(CTR_DIAG_EGL_SURFACE,error); destroyWindow(i);
    }
}

void gpuShutdown(void)
{
    if(listenerRegistered) { CtrApt_RemoveListener(lifecycle,NULL); listenerRegistered=false; }
    if(display!=EGL_NO_DISPLAY) {
        gpuVoxelAaShutdown();
        gpuVoxelScaleShutdown();
        for(int i=0;i<CTR_HOST_MAX_WINDOWS;i++) destroyWindow(i);
        for(int i=0;i<2;i++) {
            if(screens[i].data) CtrMem_Unregister(screens[i].data);
            free(screens[i].data); free(screens[i].shadow); free(screens[i].rgba);
            glDeleteFramebuffers(1,&screens[i].fbo); glDeleteTextures(1,&screens[i].texture);
        }
        glDeleteProgram(presentProgram); glDeleteVertexArrays(1,&presentVao); glDeleteBuffers(1,&presentVbo);
        eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if(pbuffer!=EGL_NO_SURFACE) eglDestroySurface(display,pbuffer);
        if(context!=EGL_NO_CONTEXT) eglDestroyContext(display,context);
        eglTerminate(display);
    }
    memset(screens,0,sizeof(screens)); memset(windows,0,sizeof(windows));
    display=EGL_NO_DISPLAY; pbuffer=EGL_NO_SURFACE; context=EGL_NO_CONTEXT; initialized=false; nextVblank=0;
    frameSchedule=(GpuFrameSchedule){0};
    CtrDiagnostics_ResetClock();
}

void gpuFlushScreens(void)
{
    if(!initialized) return;
    GLint previousRead=0,previousDraw=0;
    bool restore=false,scissored=false;
    for(unsigned i=0;i<2;i++) {
        GPU_TEXCOLOR format=screenFormat(screens[i].format); unsigned bpp=gpuPixelBytes(format);
        unsigned bytesPerColumn=240*bpp;
        u32 dirty[13];
        for(unsigned word=0;word<13;word++)
            dirty[word]=atomic_exchange_explicit(&screens[i].dirty[word],0,memory_order_acquire);
        int first=-1;
        for(unsigned x=0;x<=screens[i].columns;x++) {
            bool changed=x<screens[i].columns &&
                ((dirty[x/32]&(1u<<(x%32))) ||
                 memcmp(screens[i].data+x*bytesPerColumn,screens[i].shadow+x*bytesPerColumn,bytesPerColumn));
            if(changed) {
                if(first<0) first=(int)x;
                for(unsigned y=0;y<240;y++) gpuDecodePixel(screens[i].data+(x*240+y)*bpp,format,screens[i].rgba+(x*240+y)*4);
                memcpy(screens[i].shadow+x*bytesPerColumn,screens[i].data+x*bytesPerColumn,bytesPerColumn);
            } else if(first>=0) {
                /* Never upload a clean gap: its pixels may belong to a GPU
                 * blit and differ from the intentionally retained CPU canvas. */
                glBindTexture(GL_TEXTURE_2D,screens[i].nativeTexture);
                glTexSubImage2D(GL_TEXTURE_2D,0,0,first,240,x-(unsigned)first,GL_RGBA,GL_UNSIGNED_BYTE,screens[i].rgba+(unsigned)first*240*4);
                if(screens[i].scale>1) {
                    if(!restore) {
                        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead);
                        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previousDraw);
                        scissored=glIsEnabled(GL_SCISSOR_TEST); restore=true;
                        glDisable(GL_SCISSOR_TEST);
                    }
                    unsigned scale=screens[i].scale;
                    glBindFramebuffer(GL_READ_FRAMEBUFFER,screens[i].nativeFbo);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,screens[i].fbo);
                    glBlitFramebuffer(0,first,240,x,0,first*scale,240*scale,x*scale,
                                      GL_COLOR_BUFFER_BIT,GL_NEAREST);
                }
                first=-1;
            }
        }
    }
    if(restore) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)previousRead);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)previousDraw);
        if(scissored) glEnable(GL_SCISSOR_TEST);
    }
}

/* The CPU LCD canvas stays native-sized; only its GL presentation backing
 * grows. Copy the held image when changing size, and upload future dirty
 * columns through the native texture so they cannot erase intervening GPU
 * pixels. The caller restores framebuffer/render state after this selection. */
bool gpuScreenScale(gfxScreen_t screen,unsigned scale)
{
    if((unsigned)screen>1 || scale<1 || scale>4) return false;
    typeof(*screens) *s=&screens[screen];
    if(s->scale==scale) return true;
    if(scale>1 && gpuScaleAllocationFails(scale,3)) return false;
    GLuint texture=s->nativeTexture,fbo=s->nativeFbo;
    if(scale>1) {
        if(s->cachedScale!=scale) {
            GLuint nextTexture=0,nextFbo=0;
            glGenTextures(1,&nextTexture); glBindTexture(GL_TEXTURE_2D,nextTexture);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,240*scale,s->columns*scale,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glGenFramebuffers(1,&nextFbo); glBindFramebuffer(GL_FRAMEBUFFER,nextFbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,nextTexture,0);
            bool okay=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
            for(GLenum error;(error=glGetError())!=GL_NO_ERROR;) {
                GPU_LOG("LCD resolution allocation failed: 0x%x",error); okay=false;
            }
            if(!okay) { glDeleteFramebuffers(1,&nextFbo); glDeleteTextures(1,&nextTexture); return false; }
            /* Keep the old texture until the held image has been copied. */
            glDisable(GL_SCISSOR_TEST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,s->fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,nextFbo);
            glBlitFramebuffer(0,0,240*s->scale,s->columns*s->scale,0,0,240*scale,s->columns*scale,
                              GL_COLOR_BUFFER_BIT,GL_NEAREST);
            glDeleteFramebuffers(1,&s->scaledFbo); glDeleteTextures(1,&s->scaledTexture);
            s->scaledTexture=nextTexture; s->scaledFbo=nextFbo; s->cachedScale=scale;
            s->texture=nextTexture; s->fbo=nextFbo; s->scale=scale;
            return true;
        }
        texture=s->scaledTexture; fbo=s->scaledFbo;
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,s->fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbo);
    glBlitFramebuffer(0,0,240*s->scale,s->columns*s->scale,0,0,240*scale,s->columns*scale,
                      GL_COLOR_BUFFER_BIT,GL_NEAREST);
    s->texture=texture; s->fbo=fbo; s->scale=scale;
    return true;
}

void gpuReleaseScreenScales(void)
{
    for(unsigned i=0;i<2;i++) {
        glDeleteFramebuffers(1,&screens[i].scaledFbo); glDeleteTextures(1,&screens[i].scaledTexture);
        screens[i].scaledFbo=screens[i].scaledTexture=screens[i].cachedScale=0;
        screens[i].fbo=screens[i].nativeFbo; screens[i].texture=screens[i].nativeTexture; screens[i].scale=1;
    }
}

void gpuTransferToScreen(GpuTarget *target,gfxScreen_t screen,unsigned width,unsigned height)
{
    if((unsigned)screen>1 || !target) return;
    gpuFlushScreens();
    if(!gpuScreenScale(screen,target->scale)) {
        gpuVoxelScaleLimit(target->scale-1); gpuScreenScale(screen,1);
    }
    if(width>240) width=240;
    if(height>screens[screen].columns) height=screens[screen].columns;
    if(width>target->target->frameBuf.width) width=target->target->frameBuf.width;
    if(height>target->target->frameBuf.height) height=target->target->frameBuf.height;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,target->fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,screens[screen].fbo);
    glBlitFramebuffer(0,0,width*target->scale,height*target->scale,
                      0,0,width*screens[screen].scale,height*screens[screen].scale,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTarget?gpuTarget->fbo:0); gpuApplyState();
}
static void drawScreenRegion(int screen,CtrHostRect rect,CtrHostRect source,int width,int height,int filter)
{
    if(rect.w<=0 || rect.h<=0) return;
    float x0=2.f*rect.x/width-1, x1=2.f*(rect.x+rect.w)/width-1;
    float y0=1-2.f*rect.y/height, y1=1-2.f*(rect.y+rect.h)/height;
    /* Rotated LCD geometry: texture x runs bottom-to-top, y runs left-to-right. */
    float u0=1-source.y/240.f,u1=1-(source.y+source.h)/240.f;
    float v0=(float)source.x/screens[screen].columns,v1=(float)(source.x+source.w)/screens[screen].columns;
    const float vertices[]={x0,y0,u0,v0, x0,y1,u1,v0, x1,y0,u0,v1, x1,y1,u1,v1};
    /* A cropped menu region has real pixels just outside it (decoration or
     * navigation). Smooth scaling must clamp to this region's texel centres,
     * not bleed those neighbours into its outermost visible pixels. */
    glUniform4f(glGetUniformLocation(presentProgram,"sampleBounds"),
                u1+0.5f/(240*screens[screen].scale),v0+0.5f/(screens[screen].columns*screens[screen].scale),
                u0-0.5f/(240*screens[screen].scale),v1-0.5f/(screens[screen].columns*screens[screen].scale));
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,screens[screen].texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter?GL_LINEAR:GL_NEAREST);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
}
static void drawScreen(int screen,CtrHostRect rect,int width,int height,int filter,
                       CtrHostBottomMenuContent content)
{
    if(screen==GFX_BOTTOM) {
        CtrBottomContentRegion regions[2];
        unsigned count=CtrBottomContent_Regions(content,rect,regions);
        for(unsigned i=0;i<count;i++)
            drawScreenRegion(screen,regions[i].destination,regions[i].source,width,height,filter);
    } else drawScreenRegion(screen,rect,(CtrHostRect){0,0,400,240},width,height,filter);
}
void gpuPresent(void)
{
    if(!initialized) return;
    unsigned diagnosticEpoch=CtrDiagnostics_Epoch(),presentedSurfaces=0;
    uint64_t diagnosticStart=(diagnosticEpoch&1u)?CtrDiagnostics_NowNs():0;
#ifdef CTR_GPU_TEST
    ++presentCount;
#endif
    gpuC2DFlush(); gpuFlushScreens();
    CtrHostLayout layout; CtrHost_GetLayout(&layout);
    CtrHostBottomMenuContent content=layout.expandBottomMenus?CtrHost_BottomMenuContent():CTR_HOST_BOTTOM_ORIGINAL;
    for(int i=0;i<CTR_HOST_MAX_WINDOWS;i++) {
        refreshWindow(i);
        if(windows[i].surface==EGL_NO_SURFACE) continue;
        if(!eglMakeCurrent(display,windows[i].surface,windows[i].surface,context)) {
            CtrDiagnostics_Error(CTR_DIAG_EGL_CURRENT,eglGetError()); destroyWindow(i); continue;
        }
        /* Emulated VBlank paces the game; the 60/120 Hz displays must not add a second wait. */
        eglSwapInterval(display,0);
        EGLint width,height;
        eglQuerySurface(display,windows[i].surface,EGL_WIDTH,&width); eglQuerySurface(display,windows[i].surface,EGL_HEIGHT,&height);
        if(width<=0 || height<=0) continue;
        glBindFramebuffer(GL_FRAMEBUFFER,0); glViewport(0,0,width,height);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glClearColor(((layout.background>>16)&255)/255.f,((layout.background>>8)&255)/255.f,(layout.background&255)/255.f,1);
        glClear(GL_COLOR_BUFFER_BIT); glUseProgram(presentProgram); glUniform1i(glGetUniformLocation(presentProgram,"image"),0);
        glBindVertexArray(presentVao); glBindBuffer(GL_ARRAY_BUFFER,presentVbo);
        glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void *)0);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void *)(2*sizeof(float)));
        if(layout.topWindow==i) drawScreen(0,layout.top,width,height,layout.filter,CTR_HOST_BOTTOM_ORIGINAL);
        if(layout.bottomWindow==i) drawScreen(1,layout.bottom,width,height,layout.filter,content);
        if(!eglSwapBuffers(display,windows[i].surface)) {
            EGLint error=eglGetError(); GPU_LOG("window %d swap failed: 0x%x",i,error);
            CtrDiagnostics_Error(CTR_DIAG_EGL_SWAP,error);
            if(error==EGL_CONTEXT_LOST) CtrHost_SetState(CTR_HOST_EXITING);
            destroyWindow(i);
        } else {
            presentedSurfaces++;
            if(layout.bottomWindow==i && layout.bottom.w>0 && layout.bottom.h>0)
                CtrHost_SetPresentedBottomMenuContent(content);
        }
    }
    eglMakeCurrent(display,pbuffer,pbuffer,context);
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTarget?gpuTarget->fbo:0);
    if(gpuTarget) glViewport(0,0,gpuTarget->target->frameBuf.width*gpuTarget->scale,
                                  gpuTarget->target->frameBuf.height*gpuTarget->scale);
    gpuApplyState();
    if(diagnosticStart && presentedSurfaces)
        CtrDiagnostics_Present(diagnosticEpoch,diagnosticStart,CtrDiagnostics_NowNs(),presentedSurfaces);
}

void gfxInitDefault(void) { gfxInit(GSP_BGR8_OES,GSP_BGR8_OES,false); }
void gfxInit(GSPGPU_FramebufferFormat top,GSPGPU_FramebufferFormat bottom,bool vram)
{ (void)vram; if(gpuInit()) { gfxSetScreenFormat(GFX_TOP,top); gfxSetScreenFormat(GFX_BOTTOM,bottom); } }
void gfxExit(void) { gpuShutdown(); }
void gfxSetScreenFormat(gfxScreen_t screen,GSPGPU_FramebufferFormat format)
{ if((unsigned)screen<2 && (unsigned)format<5 && gpuInit()) { screens[screen].format=format; dirtyScreen(screen); } }
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t screen) { return (unsigned)screen<2?screens[screen].format:GSP_RGB565_OES; }
void gfxSetDoubleBuffering(gfxScreen_t screen,bool enable) { (void)screen; (void)enable; }
u8 *gfxGetFramebuffer(gfxScreen_t screen,gfx3dSide_t side,u16 *width,u16 *height)
{ (void)side; if((unsigned)screen>1 || !gpuInit()) return NULL; if(width) *width=240; if(height) *height=screens[screen].columns; return screens[screen].data; }
void gfxFlushBuffers(void) { if(initialized) { dirtyScreen(GFX_TOP); dirtyScreen(GFX_BOTTOM); } }
void gfxSwapBuffers(void) { gpuPresent(); }
void gfxSwapBuffersGpu(void) { gpuPresent(); }
void gfxSet3D(bool enable) { stereo=enable; }
bool gfxIs3D(void) { return stereo; }
void gfxSetWide(bool enable) { (void)enable; }
bool gfxIsWide(void) { return false; }
/* Origin's held-top menus bypass FrameBegin/FrameEnd. They share the same
 * simulation-tick grouping so menu animations and input also accelerate. */
void gspWaitForVBlank(void) { if(gpuInit() && gpuShouldRender()) { gpuPresent(); gpuPace(); } }
Result GSPGPU_FlushDataCache(const void *address,u32 size)
{
    __sync_synchronize();
    if(!initialized || !address || !size) return 0;
    uintptr_t begin=(uintptr_t)address,end=begin+size;
    if(end<begin) end=UINTPTR_MAX;
    for(unsigned i=0;i<2;i++) {
        unsigned stride=240*gpuPixelBytes(screenFormat(screens[i].format));
        uintptr_t base=(uintptr_t)screens[i].data,limit=base+screens[i].columns*stride;
        if(begin>=limit || end<=base) continue;
        unsigned first=(begin>base?begin-base:0)/stride;
        unsigned last=((end<limit?end:limit)-base-1)/stride;
        for(unsigned column=first;column<=last;column++)
            atomic_fetch_or_explicit(&screens[i].dirty[column/32],1u<<(column%32),memory_order_release);
    }
    return 0;
}
Result GSPGPU_InvalidateDataCache(const void *address,u32 size) { (void)address; (void)size; __sync_synchronize(); return 0; }

#ifdef CTR_GPU_TEST
/* Present-day GPU readback for the isolated conformance binary only. */
GLuint gpuTestScreenFramebuffer(gfxScreen_t screen) { return screens[screen].fbo; }
unsigned gpuTestScreenScale(gfxScreen_t screen) { return screens[screen].scale; }
double gpuTestPacingDeadline(void) { return nextVblank; }
unsigned gpuTestPresentCount(void) { return presentCount; }
/* Exercise the production LCD quad/shader against an offscreen target.
 * Native-window attachment and display assignment are tested by the app. */
void gpuTestDrawScreen(gfxScreen_t screen,CtrHostRect rect,int width,int height,int filter)
{
    gpuC2DFlush(); gpuFlushScreens();
    glViewport(0,0,width,height);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glUseProgram(presentProgram); glUniform1i(glGetUniformLocation(presentProgram,"image"),0);
    glBindVertexArray(presentVao); glBindBuffer(GL_ARRAY_BUFFER,presentVbo);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void *)0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void *)(2*sizeof(float)));
    drawScreen(screen,rect,width,height,filter,CtrHost_BottomMenuContent());
    gpuApplyState();
}
#endif
