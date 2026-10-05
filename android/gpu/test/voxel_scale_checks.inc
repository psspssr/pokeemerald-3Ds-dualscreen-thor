extern void gpuTestVoxelScaleFailure(unsigned scales,unsigned stage);
extern unsigned gpuTestVoxelScaleAllocations(void);
extern uint64_t gpuTestVoxelScaleBytes(C3D_RenderTarget *logical,C3D_RenderTarget *top,unsigned scale);
extern unsigned gpuTestScreenScale(gfxScreen_t screen);
static C3D_Tex *dioramaSurface;
#define sSurface (*dioramaSurface)
#define CTR_GAME_WIDTH 400
#define CTR_GAME_HEIGHT 240
/* Extracted unchanged by the runner: exercise the actual optional upstream
 * postprocess, including its linear/nearest sampler state transitions. */
#include "upstream_voxel_diorama.inc"
#undef CTR_GAME_WIDTH
#undef CTR_GAME_HEIGHT
#undef sSurface

static void scaleCompose(C3D_RenderTarget *logical,C3D_RenderTarget *top,C3D_Tex *surface)
{
    const Tex3DS_SubTexture visible={400,240,0,1,400.f/512,1-240.f/256};
    C2D_Prepare(); C2D_TargetClear(top,C2D_Color32(0,0,0,255));
    C2D_SceneBegin(top); C2D_ViewReset();
    C2D_DrawImageAt((C2D_Image){surface,&visible},0,0,0,NULL,1,1); C2D_Flush();
    gpuTransferToScreen(gpuFindTarget(&top->frameBuf),GFX_TOP,240,400);
    assert(logical->frameBuf.width==512 && logical->frameBuf.height==256);
}

static void voxelScaleChecks(shaderProgram_s *program,C3D_RenderTarget *restore)
{
    int maximum=voxelScaleCapabilities;
    assert(maximum>=1 && maximum<=4);
    C3D_Tex surface={0},art={0};
    assert(C3D_TexInitVRAM(&surface,512,256,GPU_RGBA8));
    assert(C3D_TexInit(&art,8,8,GPU_RGBA5551));
    for(unsigned y=0;y<8;y++) for(unsigned x=0;x<8;x++)
        ((u16 *)art.data)[morton(x,y)]=((x+y)&1)?0xF801:0x07FF;
    const Tex3DS_SubTexture artSub={8,8,0,1,1,0};
    C3D_RenderTarget *logical=C3D_RenderTargetCreateFromTex(&surface,GPU_TEXFACE_2D,0,GPU_RB_DEPTH16);
    C3D_RenderTarget *top=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);
    assert(logical && top);
    uint64_t budget=gpuTestVoxelScaleBytes(logical,top,4);
    assert(budget>96u*1024u*1024u && budget<128u*1024u*1024u); checks++;
    C3D_RenderTarget huge={.frameBuf={.width=4096,.height=4096}};
    assert(gpuTestVoxelScaleBytes(&huge,&huge,4)>UINT32_MAX); checks++; // ARM32 must not wrap the cap
    GpuTarget *scene=gpuFindTarget(&logical->frameBuf),*screen=gpuFindTarget(&top->frameBuf);
    gfxSetScreenFormat(GFX_TOP,GSP_RGB565_OES);
    u16 *cpu=(u16 *)gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);
    memset(cpu,0,240*400*2); GSPGPU_FlushDataCache(cpu,240*400*2); gpuFlushScreens();
    const void *nativeData=surface.data; size_t nativeSize=surface.size;
    GLuint lastScene=0,lastTop=0,lastLcd=0;
    for(int scale=1;scale<=maximum;scale++) {
        voxelScaleRequested=scale;
        assert(CtrGpu_ConfigureVoxelTargets(logical,top,true,false)==(unsigned)scale);
        assert(scene->scale==(unsigned)scale && screen->scale==(unsigned)scale);
        assert(surface.width==512 && surface.height==256 && surface.size==nativeSize && surface.data==nativeData);
        unsigned allocated=gpuTestVoxelScaleAllocations();
        assert(CtrGpu_ConfigureVoxelTargets(logical,top,true,false)==(unsigned)scale);
        assert(gpuTestVoxelScaleAllocations()==allocated); checks++;
        unsigned width=240*scale,height=400*scale;
        unsigned char *lcd=malloc((size_t)width*height*4);
        unsigned char *canvasPixels=malloc((size_t)width*height*4);
        assert(lcd && canvasPixels);
        C3D_RenderTarget *canvas=C3D_RenderTargetCreate(height,width,GPU_RB_RGBA8,-1);
        assert(canvas);
        for(unsigned samples=0;samples<=4;samples+=2) {
            voxelAaRequested=(int)samples;
            C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
            C2D_TargetClear(logical,C2D_Color32(0,0,0,255)); C3D_FrameDrawOn(logical);
            glDepthRangef(0,1);
            bool aa=CtrGpu_BeginVoxelAA(logical);
            aaTriangle(program,-410,1,1,1);
            if(aa) CtrGpu_EndVoxelAA();
            aaTriangle(program,-100,0,0,1);
            pixel(100*scale,80*scale,255,255,255,255); // resolved depth preserves terrain
            C2D_Prepare(); C2D_SceneBegin(logical); C2D_ViewReset();
            C2D_DrawImageAt((C2D_Image){&art,&artSub},12,9,0,NULL,1,1);
            C2D_Flush();
            scaleCompose(logical,top,&surface);
            glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_TOP));
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,lcd);
            assert(glGetError()==GL_NO_ERROR);
            unsigned detailed=0;
            for(unsigned x=0;x<400;x++) for(unsigned y=0;y<240;y++) {
                unsigned base=((x*scale)*width+(239-y)*scale)*4;
                const unsigned char *first=lcd+base;
                bool varied=false;
                for(int dx=0;dx<scale;dx++) for(int dy=0;dy<scale;dy++) {
                    const unsigned char *p=lcd+base+((unsigned)dx*width+dy)*4;
                    assert(p[3]==255);
                    if(memcmp(first,p,3)) varied=true;
                    if(x>=12 && x<20 && y>=9 && y<17) {
                        bool red=((x-12+y-9)&1)!=0;
                        assert(p[0]==(red?255:0) && p[1]==(red?0:255) && p[2]==(red?0:255));
                    }
                }
                detailed+=varied;
            }
            /* A 400x240 render enlarged later cannot create any within-pixel
             * edge detail; this assertion observes the final LCD backing. */
            assert(scale==1?detailed==0:detailed>100); checks++;
            C3D_FrameDrawOn(canvas);
            gpuTestDrawScreen(GFX_TOP,(CtrHostRect){0,0,(int)height,(int)width},height,width,0);
            glReadPixels(0,0,height,width,GL_RGBA,GL_UNSIGNED_BYTE,canvasPixels);
            for(unsigned y=0;y<width;y++) for(unsigned x=0;x<height;x++)
                assert(!memcmp(canvasPixels+((size_t)y*height+x)*4,lcd+((size_t)x*width+y)*4,4));
            checks++;
            printf("VOXEL resolution %dx, MSAA%u: %u logical pixels contain newly rendered subpixel detail; exact native UI and rotated presentation preserved\n",scale,samples,detailed);
        }
        voxelAaRequested=0;
        /* Upstream blurs the world's periphery, not its central focus band.
         * The native UI is drawn afterwards, exactly as RenderVoxel does. */
        C2D_Prepare(); C2D_SceneBegin(top); C2D_ViewReset();
        dioramaSurface=&surface; VoxelDiorama();
        C2D_DrawImageAt((C2D_Image){&art,&artSub},12,9,0,NULL,1,1); C2D_Flush();
        gpuTransferToScreen(screen,GFX_TOP,240,400);
        glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_TOP));
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,canvasPixels);
        unsigned peripheralChanges=0;
        for(unsigned x=0;x<400;x++) for(unsigned y=0;y<240;y++)
            for(int dx=0;dx<scale;dx++) for(int dy=0;dy<scale;dy++) {
                size_t offset=((size_t)(x*scale+dx)*width+(239-y)*scale+dy)*4;
                bool changed=memcmp(lcd+offset,canvasPixels+offset,4)!=0;
                if(y>=100 && y<184) assert(!changed);
                else peripheralChanges+=changed;
                if(x>=12 && x<20 && y>=9 && y<17) {
                    const unsigned char *p=canvasPixels+offset; bool red=((x+y-21)&1)!=0;
                    assert(p[0]==(red?255:0) && p[1]==(red?0:255) && p[2]==(red?0:255) && p[3]==255);
                }
            }
        assert(peripheralChanges>0 && !(surface.param&6)); checks++;
        memcpy(lcd,canvasPixels,(size_t)width*height*4);
        /* Held-top menus switch their own logical surface to1x, while the
         * top texture and LCD keep the preceding high-resolution picture. */
        assert(CtrGpu_ConfigureVoxelTargets(logical,top,false,true)==1);
        assert(scene->scale==1 && screen->scale==(unsigned)scale && gpuTestScreenScale(GFX_TOP)==(unsigned)scale);
        glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_TOP));
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,canvasPixels);
        assert(!memcmp(lcd,canvasPixels,(size_t)width*height*4)); checks++;
        if(scale==maximum) {
            gpuListener(CTR_APT_SUSPEND,NULL); gpuListener(CTR_APT_RESUME,NULL);
            glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_TOP));
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,canvasPixels);
            assert(!memcmp(lcd,canvasPixels,(size_t)width*height*4)); checks++;
        }
        /* Dirty CPU columns commit at logical pixel size, never overwrite
         * the untouched high-resolution GPU region between those columns. */
        for(unsigned y=0;y<240;y++) { cpu[40*240+y]=0x001f; cpu[360*240+y]=0x07e0; }
        GSPGPU_FlushDataCache(cpu+40*240,480); GSPGPU_FlushDataCache(cpu+360*240,480);
        gpuFlushScreens();
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,canvasPixels);
        for(unsigned x=0;x<width;x++) for(int dy=0;dy<scale;dy++) {
            const unsigned char *blue=canvasPixels+((size_t)(40*scale+dy)*width+x)*4;
            const unsigned char *green=canvasPixels+((size_t)(360*scale+dy)*width+x)*4;
            assert(blue[0]==0 && blue[1]==0 && blue[2]==255 && blue[3]==255);
            assert(green[0]==0 && green[1]==255 && green[2]==0 && green[3]==255);
        }
        assert(!memcmp(canvasPixels+(size_t)200*scale*width*4,lcd+(size_t)200*scale*width*4,(size_t)scale*width*4)); checks++;
        C3D_RenderTargetDelete(canvas); free(lcd); free(canvasPixels);
        lastScene=scene->scaledFbo; lastTop=screen->scaledFbo; lastLcd=gpuTestScreenFramebuffer(GFX_TOP);
        assert(CtrGpu_ConfigureVoxelTargets(logical,top,false,false)==1);
        assert(scene->scale==1 && screen->scale==1 && gpuTestScreenScale(GFX_TOP)==(unsigned)scale);
        C2D_TargetClear(top,C2D_Color32(255,0,255,255));
        gpuTransferToScreen(screen,GFX_TOP,240,400);
        assert(gpuTestScreenScale(GFX_TOP)==1); checks++;
    }
    /* Scissor and explicit viewport retain the same native coordinate API. */
    if(maximum>1) {
        assert(CtrGpu_ConfigureVoxelTargets(logical,top,true,false)==(unsigned)maximum);
        C2D_Prepare(); C2D_SceneBegin(logical); C2D_ViewReset(); C2D_TargetClear(logical,0);
        C3D_SetScissor(GPU_SCISSOR_NORMAL,10,20,12,23);
        C2D_DrawRectSolid(0,0,0,512,256,C2D_Color32(255,0,0,255)); C2D_Flush();
        pixel(10*maximum,20*maximum,255,0,0,255); pixel(10*maximum-1,20*maximum,0,0,0,0);
        C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
        C3D_SetViewport(7,9,30,40); GLint viewport[4]; glGetIntegerv(GL_VIEWPORT,viewport);
        assert(viewport[0]==7*maximum && viewport[1]==9*maximum && viewport[2]==30*maximum && viewport[3]==40*maximum); checks++;
    }
    gpuVoxelScaleShutdown();
    assert(voxelScaleCapabilities==-1 && scene->scale==1 && screen->scale==1);
    assert(!lastScene || !glIsFramebuffer(lastScene)); assert(!lastTop || !glIsFramebuffer(lastTop));
    if(maximum>1) assert(!glIsFramebuffer(lastLcd)); checks++;
    gpuVoxelScaleInit(); assert(voxelScaleCapabilities==maximum);
    for(unsigned stage=1;maximum>1 && stage<=3;stage++) {
        voxelScaleRequested=maximum;
        assert(CtrGpu_ConfigureVoxelTargets(logical,top,true,false)==(unsigned)maximum);
        gpuScreenScale(GFX_TOP,1); // force an LCD reselect for injected stage3 failure
        gpuTestVoxelScaleFailure(1u<<maximum,stage);
        assert(CtrGpu_ConfigureVoxelTargets(logical,top,true,false)==(unsigned)maximum-1);
        assert(voxelScaleCapabilities==maximum-1 && scene->scale==screen->scale); checks++;
        assert(scene->cachedScale<=(unsigned)maximum-1 && screen->cachedScale<=(unsigned)maximum-1);
        GLint previous,actualWidth,actualHeight;
        glGetIntegerv(GL_RENDERBUFFER_BINDING,&previous); glBindRenderbuffer(GL_RENDERBUFFER,scene->depth);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_WIDTH,&actualWidth);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_HEIGHT,&actualHeight);
        assert(actualWidth==512*(maximum-1) && actualHeight==256*(maximum-1));
        glBindRenderbuffer(GL_RENDERBUFFER,(GLuint)previous); checks++;
        gpuVoxelScaleInit(); assert(voxelScaleCapabilities==maximum-1); // no retry storm
        gpuTestVoxelScaleFailure(0,0); gpuVoxelScaleShutdown(); gpuVoxelScaleInit();
    }
    if(maximum>1) {
        voxelScaleRequested=maximum;
        assert(CtrGpu_ConfigureVoxelTargets(logical,top,true,false)==(unsigned)maximum);
        GLuint oldScene=scene->scaledFbo,oldTop=screen->scaledFbo,oldLcd=gpuTestScreenFramebuffer(GFX_TOP);
        gpuTestVoxelScaleFailure(0x1c,2);
        assert(CtrGpu_ConfigureVoxelTargets(logical,top,true,false)==1 && voxelScaleCapabilities==1);
        assert(!scene->scaledFbo && !screen->scaledFbo && !gpuTestVoxelAaFramebuffer());
        assert(!glIsFramebuffer(oldScene) && !glIsFramebuffer(oldTop) && !glIsFramebuffer(oldLcd));
        assert(gpuTestScreenScale(GFX_TOP)==1); checks++;
        gpuTestVoxelScaleFailure(0,0); gpuVoxelScaleShutdown(); gpuVoxelScaleInit();
    }
    voxelScaleRequested=4;
    assert(CtrGpu_ConfigureVoxelTargets(logical,top,false,false)==1);
    C2D_Prepare(); C2D_SceneBegin(top); C2D_ViewReset(); C2D_TargetClear(top,C2D_Color32(255,0,255,255));
    gpuTransferToScreen(screen,GFX_TOP,240,400);
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_TOP));
    pixel(239,399,255,0,255,255); // classic frame restores native-sized LCD
    /* A render target's lifetime can be shorter than its texture's. The
     * retained native texture must contain the last rendered color image. */
    voxelScaleRequested=maximum;
    CtrGpu_ConfigureVoxelTargets(logical,top,true,false);
    C2D_TargetClear(logical,C2D_Color32(18,42,86,255));
    C3D_RenderTargetDelete(logical);
    C2D_Prepare(); C2D_SceneBegin(restore); C2D_ViewReset();
    const Tex3DS_SubTexture retained={16,16,0,1,16.f/512,1-16.f/256};
    C2D_DrawImageAt((C2D_Image){&surface,&retained},0,0,0,NULL,1,1);
    pixel(2,2,18,42,86,255);
    voxelScaleRequested=1;
    C3D_RenderTargetDelete(top);
    C3D_TexDelete(&surface); C3D_TexDelete(&art);
    C2D_Prepare(); C2D_SceneBegin(restore); C2D_ViewReset();
    assert(glGetError()==GL_NO_ERROR);
}

static void voxelScaleReinitChecks(void)
{
    assert(voxelScaleCapabilities==-1);
    assert(C3D_Init(0) && C2D_Init(128));
    assert(voxelScaleCapabilities>=1 && voxelScaleCapabilities<=4);
    int maximum=voxelScaleCapabilities;
    C3D_RenderTarget *scene=C3D_RenderTargetCreate(32,32,GPU_RB_RGBA8,GPU_RB_DEPTH16);
    C3D_RenderTarget *top=C3D_RenderTargetCreate(32,32,GPU_RB_RGBA8,-1);
    assert(scene && top);
    voxelScaleRequested=maximum;
    assert(CtrGpu_ConfigureVoxelTargets(scene,top,true,false)==(unsigned)maximum);
    C2D_Prepare(); C2D_SceneBegin(scene); C2D_ViewReset();
    C2D_TargetClear(scene,C2D_Color32(0,255,255,255));
    pixel(32*maximum-1,32*maximum-1,0,255,255,255);
    GLuint frame=gpuFindTarget(&scene->frameBuf)->fbo;
    C2D_Fini(); C3D_Fini();
    assert(voxelScaleCapabilities==-1 && !glIsFramebuffer(frame)); checks++;
    voxelScaleRequested=1;
}
