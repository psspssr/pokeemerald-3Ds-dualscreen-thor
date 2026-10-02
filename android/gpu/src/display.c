#include "gpu_internal.h"
#include <android/native_window.h>
#include <time.h>
#include <errno.h>
#include <ctrshim_apt.h>

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
    unsigned columns;
} screens[2];
static GLuint presentProgram,presentVbo,presentVao;
static double nextVblank;
static double framePeriod=1.0/59.83;
static bool listenerRegistered;
static void destroyWindow(int i);

static void lifecycle(CtrAptEvent event,void *user)
{
    (void)user;
    if(event==CTR_APT_SUSPEND || event==CTR_APT_EXIT) {
        gpuC2DFlush();
        for(int i=0;i<CTR_HOST_MAX_WINDOWS;i++) destroyWindow(i);
    }
    /* A resumed game starts a fresh pacing interval, with its FBOs retained. */
    nextVblank=0;
}

double gpuNow(void)
{ struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now); return now.tv_sec+now.tv_nsec*1e-9; }
void gpuPace(void)
{
    double now=gpuNow();
    if(!nextVblank || now>nextVblank+framePeriod*4) nextVblank=now;
    nextVblank+=framePeriod;
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
    const char *fs="#version 300 es\nprecision mediump float;in vec2 tc;uniform sampler2D image;out vec4 color;void main(){color=vec4(texture(image,tc).rgb,1);}";
    GLuint vertex=gpuCompile(GL_VERTEX_SHADER,vs),fragment=gpuCompile(GL_FRAGMENT_SHADER,fs);
    presentProgram=gpuLink(vertex,fragment); glDeleteShader(vertex); glDeleteShader(fragment);
    if(!presentProgram) goto fail;
    glGenVertexArrays(1,&presentVao); glGenBuffers(1,&presentVbo);
    for(int i=0;i<2;i++) {
        screens[i].columns=i?320:400; screens[i].format=GSP_BGR8_OES;
        size_t maxSize=(size_t)screens[i].columns*240*4;
        screens[i].data=calloc(1,maxSize); screens[i].shadow=malloc(maxSize); screens[i].rgba=calloc(1,maxSize);
        if(!screens[i].data || !screens[i].shadow || !screens[i].rgba) goto fail;
        memset(screens[i].shadow,0xff,maxSize);
        CtrMem_Register(CTR_MEM_FRAMEBUFFER,screens[i].data,maxSize,&screens[i]);
        glGenTextures(1,&screens[i].texture); glBindTexture(GL_TEXTURE_2D,screens[i].texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,240,screens[i].columns,0,GL_RGBA,GL_UNSIGNED_BYTE,screens[i].rgba);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1,&screens[i].fbo); glBindFramebuffer(GL_FRAMEBUFFER,screens[i].fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,screens[i].texture,0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0); glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    listenerRegistered=CtrApt_AddListener(lifecycle,NULL);
    if(!listenerRegistered) goto fail;
    initialized=true;
    __android_log_print(ANDROID_LOG_INFO,"EmeraldGPU","GLES %s, %s",glGetString(GL_VERSION),glGetString(GL_RENDERER));
    return true;
fail:
    GPU_LOG("EGL/GLES initialization failed: 0x%x",eglGetError());
    gpuShutdown(); return false;
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
    if(windows[i].surface==EGL_NO_SURFACE) { GPU_LOG("window %d surface creation failed: 0x%x",i,eglGetError()); destroyWindow(i); }
}

void gpuShutdown(void)
{
    if(listenerRegistered) { CtrApt_RemoveListener(lifecycle,NULL); listenerRegistered=false; }
    if(display!=EGL_NO_DISPLAY) {
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
    display=EGL_NO_DISPLAY; pbuffer=EGL_NO_SURFACE; context=EGL_NO_CONTEXT; initialized=false;
}

void gpuFlushScreens(void)
{
    if(!initialized) return;
    for(unsigned i=0;i<2;i++) {
        GPU_TEXCOLOR format=screenFormat(screens[i].format); unsigned bpp=gpuPixelBytes(format);
        unsigned bytesPerColumn=240*bpp,first=screens[i].columns,last=0;
        for(unsigned x=0;x<screens[i].columns;x++) {
            if(!memcmp(screens[i].data+x*bytesPerColumn,screens[i].shadow+x*bytesPerColumn,bytesPerColumn)) continue;
            if(x<first) first=x; last=x+1;
            for(unsigned y=0;y<240;y++) gpuDecodePixel(screens[i].data+(x*240+y)*bpp,format,screens[i].rgba+(x*240+y)*4);
            memcpy(screens[i].shadow+x*bytesPerColumn,screens[i].data+x*bytesPerColumn,bytesPerColumn);
        }
        if(first<last) {
            glBindTexture(GL_TEXTURE_2D,screens[i].texture);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,first,240,last-first,GL_RGBA,GL_UNSIGNED_BYTE,screens[i].rgba+first*240*4);
        }
    }
}

void gpuTransferToScreen(GpuTarget *target,gfxScreen_t screen,unsigned width,unsigned height)
{
    if((unsigned)screen>1 || !target) return;
    gpuFlushScreens();
    if(width>240) width=240;
    if(height>screens[screen].columns) height=screens[screen].columns;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,target->fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER,screens[screen].fbo);
    glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTarget?gpuTarget->fbo:0); gpuApplyState();
}
static void drawScreen(int screen,CtrHostRect rect,int width,int height,int filter)
{
    if(rect.w<=0 || rect.h<=0) return;
    float x0=2.f*rect.x/width-1, x1=2.f*(rect.x+rect.w)/width-1;
    float y0=1-2.f*rect.y/height, y1=1-2.f*(rect.y+rect.h)/height;
    /* Rotated LCD geometry: texture x runs bottom-to-top, y runs left-to-right. */
    const float vertices[]={x0,y0,1,0, x0,y1,0,0, x1,y0,1,1, x1,y1,0,1};
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,screens[screen].texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter?GL_LINEAR:GL_NEAREST);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
}
void gpuPresent(void)
{
    if(!initialized) return;
    gpuC2DFlush(); gpuFlushScreens();
    CtrHostLayout layout; CtrHost_GetLayout(&layout);
    for(int i=0;i<CTR_HOST_MAX_WINDOWS;i++) {
        refreshWindow(i);
        if(windows[i].surface==EGL_NO_SURFACE) continue;
        if(!eglMakeCurrent(display,windows[i].surface,windows[i].surface,context)) { destroyWindow(i); continue; }
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
        if(layout.topWindow==i) drawScreen(0,layout.top,width,height,layout.filter);
        if(layout.bottomWindow==i) drawScreen(1,layout.bottom,width,height,layout.filter);
        if(!eglSwapBuffers(display,windows[i].surface)) {
            EGLint error=eglGetError(); GPU_LOG("window %d swap failed: 0x%x",i,error);
            if(error==EGL_CONTEXT_LOST) CtrHost_SetState(CTR_HOST_EXITING);
            destroyWindow(i);
        }
    }
    eglMakeCurrent(display,pbuffer,pbuffer,context);
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTarget?gpuTarget->fbo:0);
    if(gpuTarget) glViewport(0,0,gpuTarget->target->frameBuf.width,gpuTarget->target->frameBuf.height);
    gpuApplyState();
}

void gfxInitDefault(void) { gfxInit(GSP_BGR8_OES,GSP_BGR8_OES,false); }
void gfxInit(GSPGPU_FramebufferFormat top,GSPGPU_FramebufferFormat bottom,bool vram)
{ (void)vram; if(gpuInit()) { gfxSetScreenFormat(GFX_TOP,top); gfxSetScreenFormat(GFX_BOTTOM,bottom); } }
void gfxExit(void) { gpuShutdown(); }
void gfxSetScreenFormat(gfxScreen_t screen,GSPGPU_FramebufferFormat format)
{ if((unsigned)screen<2 && (unsigned)format<5 && gpuInit()) { screens[screen].format=format; memset(screens[screen].shadow,0xff,(size_t)screens[screen].columns*240*4); } }
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t screen) { return (unsigned)screen<2?screens[screen].format:GSP_RGB565_OES; }
void gfxSetDoubleBuffering(gfxScreen_t screen,bool enable) { (void)screen; (void)enable; }
u8 *gfxGetFramebuffer(gfxScreen_t screen,gfx3dSide_t side,u16 *width,u16 *height)
{ (void)side; if((unsigned)screen>1 || !gpuInit()) return NULL; if(width) *width=240; if(height) *height=screens[screen].columns; return screens[screen].data; }
void gfxFlushBuffers(void) { /* CPU writes are compared and uploaded at the next presentation. */ }
void gfxSwapBuffers(void) { gpuPresent(); }
void gfxSwapBuffersGpu(void) { gpuPresent(); }
void gfxSet3D(bool enable) { stereo=enable; }
bool gfxIs3D(void) { return stereo; }
void gfxSetWide(bool enable) { (void)enable; }
bool gfxIsWide(void) { return false; }
void gspWaitForVBlank(void) { gpuPresent(); gpuPace(); }
Result GSPGPU_FlushDataCache(const void *address,u32 size) { (void)address; (void)size; __sync_synchronize(); return 0; }
Result GSPGPU_InvalidateDataCache(const void *address,u32 size) { (void)address; (void)size; __sync_synchronize(); return 0; }

#ifdef CTR_GPU_TEST
/* Present-day GPU readback for the isolated conformance binary only. */
GLuint gpuTestScreenFramebuffer(gfxScreen_t screen) { return screens[screen].fbo; }
#endif
