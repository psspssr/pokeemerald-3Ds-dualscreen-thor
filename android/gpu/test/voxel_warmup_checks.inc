/* This is the actual blank dispatch body extracted after strict overlays.
 * No DrawFps/SceneBegin outside that body may accidentally submit the clear. */
static void voxelWarmupBlank(C3D_RenderTarget *sTop)
{
    unsigned sPlanes=7;
#include "upstream_voxel_warmup.inc"
    assert(sPlanes==0);
}

static void voxelWarmupChecks(C3D_RenderTarget *restore)
{
    C3D_RenderTarget *logical=C3D_RenderTargetCreate(512,256,GPU_RB_RGBA8,GPU_RB_DEPTH16);
    C3D_RenderTarget *top=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);
    assert(logical && top);
    const unsigned scales[]={1,(unsigned)voxelScaleCapabilities};
    for(unsigned i=0;i<sizeof(scales)/sizeof(*scales);i++) {
        unsigned scale=scales[i];
        voxelScaleRequested=(int)scale;
        assert(C3D_FrameBegin(0));
        assert(CtrGpu_ConfigureVoxelTargets(logical,top,true,false)==scale);
        C2D_Prepare(); C2D_SceneBegin(top);
        C2D_TargetClear(top,C2D_Color32(230,42,18,255));
        C3D_FrameEnd(0);
        glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_TOP));
        pixel(120*scale,200*scale,230,42,18,255);

        assert(C3D_FrameBegin(0));
        /* The real dispatch has voxel=false while the next map warms. */
        assert(CtrGpu_ConfigureVoxelTargets(logical,top,false,false)==1);
        voxelWarmupBlank(top);
        C3D_FrameEnd(0);
        glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_TOP));
        pixel(120,200,0,0,0,255);
        assert(gpuTestScreenScale(GFX_TOP)==1);
        unsigned char *lcd=malloc(240*400*4);
        assert(lcd);
        glReadPixels(0,0,240,400,GL_RGBA,GL_UNSIGNED_BYTE,lcd);
        for(unsigned p=0;p<240*400;p++)
            assert(lcd[p*4]==0 && lcd[p*4+1]==0 && lcd[p*4+2]==0 && lcd[p*4+3]==255);
        free(lcd); checks++;

        assert(C3D_FrameBegin(0));
        assert(CtrGpu_ConfigureVoxelTargets(logical,top,true,false)==scale);
        C2D_Prepare(); C2D_SceneBegin(top);
        C2D_TargetClear(top,C2D_Color32(12,180,84,255));
        C3D_FrameEnd(0);
        glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_TOP));
        pixel(120*scale,200*scale,12,180,84,255);
        printf("PASS actual voxel warm-up blank with FPS off: %ux drawn scene -> black LCD -> new scene\n",scale);
    }
    voxelScaleRequested=1;
    C3D_RenderTargetDelete(logical); C3D_RenderTargetDelete(top);
    C2D_Prepare(); C2D_SceneBegin(restore); C2D_ViewReset();
}
