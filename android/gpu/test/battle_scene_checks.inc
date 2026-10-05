#include <ctr_gpu_battle.h>
#include <3ds_video.h>

/* Keep the production allocation, transforms, masks and sampling calls.
 * Compose supplies a deterministic native sprite and opaque HP/text pattern
 * instead of requiring a running battle engine in this renderer fixture. */
static C3D_Tex sSceneTex,sBattleShadowTex,sBloomTex,fixtureSprite,fixtureHud;
static C3D_RenderTarget *sScene,*sLogical,*sBloom;
static bool sSceneFailed,sBattleShadowFailed,sBattle,sTransition,sBattleWorld,fixtureStats;
static u32 sSceneUsedFrame,sSceneFailFrame;
static CtrVideoStats sStats;
static float sZoom,sOffX,sOffY;
static int sViewX,sViewY,sTargetW,sTargetH,sSurfaceH;
static unsigned sLayerExclude,sWorldLayers,sceneCompositions;
static uint16_t lineRegisters[CTR_GBA_LINES][CTR_LINE_REGS];
static const uint16_t (*sLineRegs)[CTR_LINE_REGS]=lineRegisters;
static unsigned Min(unsigned a,unsigned b) { return a<b?a:b; }
static unsigned Reg(unsigned offset)
{ return offset==0?0x1f00:offset==8?(fixtureStats?1:0):offset==10?(fixtureStats?0:1):0; }
static void ClipToView(void) { C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0); }
static void BlendForget(void) {}
static void Blend(int a,bool b,bool c)
{
    (void)a;(void)b;(void)c;
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
}
static void GpuSplit(void) { C3D_FrameSplit(0); }
static unsigned CtrVoxel_ReleaseIdleVram(void) { return 0; }
static void CtrLog_Write(int level,const char *format,...) { (void)level;(void)format; }
#define CTR_LOG_ERROR 0
#define CTR_LOG_VIDEO 1
#define CTR_VOXEL_ENABLED 1
static void Compose(void)
{
    assert((sLayerExclude&1u) && (sLayerExclude&16u)==0);
    assert(((sLayerExclude&2u)!=0)==fixtureStats);
    const Tex3DS_SubTexture sprite={8,8,0,1,1,0},hud={32,8,0,1,1,0};
    C2D_DrawImageAt((C2D_Image){&fixtureSprite,&sprite},(48+sViewX)*sZoom+sOffX,
                    (32+sViewY)*sZoom+sOffY,0,NULL,sZoom,sZoom);
    C2D_DrawImageAt((C2D_Image){&fixtureHud,&hud},(136+sViewX)*sZoom+sOffX,
                    (24+sViewY)*sZoom+sOffY,0,NULL,sZoom,sZoom);
    sceneCompositions++;
}
#define sSurface (*dioramaSurface)
#include "upstream_battle_scene.inc"
#undef sSurface

static unsigned paletteIndex(const unsigned char *p)
{
    const unsigned char palette[4][3]={{255,0,0},{0,255,255},{255,255,255},{0,0,0}};
    for(unsigned i=0;i<4;i++) if(!memcmp(p,palette[i],3)) return i;
    return 4;
}

static void battleLcdPalette(const unsigned char *pixels,const unsigned char *background,
                             unsigned scale,bool smooth,const char *label)
{
    unsigned w=240*scale,counts[4]={0},holes=0,mixed=0;
    /* Both actual transformed footprints, including transparent surroundings. */
    const float ox=(CTR_GAME_WIDTH-240*CTR_BATTLE_ZOOM)/2;
    const float oy=CTR_GAME_HEIGHT-48-112*CTR_BATTLE_ZOOM;
    const int art[2][4]={{48,32,8,8},{136,24,32,8}};
    for(unsigned r=0;r<2;r++) {
        int left=(int)floorf((art[r][0]*CTR_BATTLE_ZOOM+ox-1)*scale);
        int right=(int)ceilf(((art[r][0]+art[r][2])*CTR_BATTLE_ZOOM+ox+1)*scale);
        int top=(int)floorf((art[r][1]*CTR_BATTLE_ZOOM+oy-1)*scale);
        int bottom=(int)ceilf(((art[r][1]+art[r][3])*CTR_BATTLE_ZOOM+oy+1)*scale);
        for(int x=left;x<right;x++) for(int y=top;y<bottom;y++) {
            size_t offset=((size_t)x*w+(w-1-y))*4;
            const unsigned char *p=pixels+offset;
            unsigned color=paletteIndex(p);
            if((r==0 && color<2) || (r==1 && color>=2 && color<4)) counts[color]++;
            else if(!memcmp(p,background+offset,4)) holes++;
            else mixed++;
            assert(p[3]==255);
        }
    }
    if(!smooth) {
        /* Use the interior of a 2x2 source hole. Skip its resampling boundary
         * so a driver's tie-breaking cannot turn this into a brittle test. */
        int left=(int)ceilf(((48+3)*CTR_BATTLE_ZOOM+ox)*scale+0.5f);
        int right=(int)floorf(((48+5)*CTR_BATTLE_ZOOM+ox)*scale-0.5f);
        int top=(int)ceilf(((32+3)*CTR_BATTLE_ZOOM+oy)*scale+0.5f);
        int bottom=(int)floorf(((32+5)*CTR_BATTLE_ZOOM+oy)*scale-0.5f);
        assert(left<=right && top<=bottom);
        for(int x=left;x<=right;x++) for(int y=top;y<=bottom;y++) {
            size_t hole=((size_t)x*w+(w-1-y))*4;
            assert(!memcmp(pixels+hole,background+hole,4));
        }
    }
    if((mixed>0)!=smooth || !counts[0] || !counts[1] || !counts[2] || !counts[3] || !holes) {
        fprintf(stderr,"FAIL battle %s scale%u %s: mixed=%u palette=%u/%u/%u/%u holes=%u (actual zoom%.2f / scene%.2f)\n",
                label,scale,smooth?"Smooth":"Sharp",mixed,counts[0],counts[1],counts[2],counts[3],holes,
                (double)CTR_BATTLE_ZOOM,(double)SCENE_ZOOM);abort();
    }
    checks++;
}

static void battlePhysicalPalette(C3D_RenderTarget *canvas,const unsigned char *background,bool smooth,
                                  unsigned scale,unsigned voxel,unsigned aa)
{
    unsigned char *rgba=malloc(1920u*1080u*4);assert(rgba);
    C3D_FrameDrawOn(canvas);
    /* Nearest final sampling isolates the intermediate filter: Smooth must
     * already contain blended colors, Sharp must not invent any. */
    gpuTestDrawScreen(GFX_TOP,(CtrHostRect){0,0,1920,1080},1920,1080,0);
    glReadPixels(0,0,1920,1080,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    unsigned colors[4]={0},mixed=0,holes=0;
    const float ox=(CTR_GAME_WIDTH-240*CTR_BATTLE_ZOOM)/2;
    const float oy=CTR_GAME_HEIGHT-48-112*CTR_BATTLE_ZOOM;
    int x0=(int)floorf((48*CTR_BATTLE_ZOOM+ox-1)*1920/400);
    int x1=(int)ceilf((56*CTR_BATTLE_ZOOM+ox+1)*1920/400);
    int y0=(int)floorf((32*CTR_BATTLE_ZOOM+oy-1)*1080/240);
    int y1=(int)ceilf((40*CTR_BATTLE_ZOOM+oy+1)*1080/240);
    for(int y=y0;y<y1;y++) for(int x=x0;x<x1;x++) {
        size_t offset=((size_t)(1079-y)*1920+x)*4;
        unsigned color=paletteIndex(rgba+offset);
        if(color<2) colors[color]++;
        else if(!memcmp(rgba+offset,background+offset,4)) holes++;
        else mixed++;
    }
    if(!colors[0] || !colors[1] || !holes || ((mixed>0)!=smooth)) {
        fprintf(stderr,"FAIL physical battle scale%u voxel%u AA%u %s: colors=%u/%u/%u/%u holes=%u mixed=%u\n",
                scale,voxel,aa,smooth?"Smooth":"Sharp",colors[0],colors[1],colors[2],colors[3],holes,mixed);
        abort();
    }
    checks++;
    free(rgba);
}

static void battleEffectsPreserved(C3D_RenderTarget *canvas,C3D_Tex *surface)
{
    unsigned char *baseline=malloc(400u*240u*4),*actual=malloc(400u*240u*4);assert(baseline&&actual);
    assert(C3D_TexInitVRAM(&sBloomTex,128,64,GPU_RGB565));
    C3D_TexSetFilter(&sBloomTex,GPU_LINEAR,GPU_LINEAR);
    sBloom=C3D_RenderTargetCreateFromTex(&sBloomTex,GPU_TEXFACE_2D,0,-1);assert(sBloom);
    dioramaSurface=surface;
    for(unsigned effect=0;effect<2;effect++) {
        for(unsigned mode=0;mode<2;mode++) {
            imageFilter=mode==0;ScenePrepare();
            C2D_Prepare();C2D_ViewReset();
            if(effect==1) {
                C2D_SceneBegin(sLogical);C2D_TargetClear(sLogical,C2D_Color32(0,0,0,255));
                C2D_DrawRectSolid(81,81,0,39,19,C2D_Color32(255,255,255,255));C2D_Flush();
                VoxelBloomPrepare();
                assert((surface->param&6)==0 && (sBloomTex.param&6)==6);
            }
            C2D_SceneBegin(canvas);C2D_ViewReset();
            C2D_TargetClear(canvas,effect?C2D_Color32(0,0,0,255):C2D_Color32(255,255,255,255));
            if(effect) VoxelBloomCompose(0.5f);
            else {
                const Tex3DS_SubTexture shadow={BATTLE_SHADOW_W,BATTLE_SHADOW_H,0,1,1,0};
                C2D_DrawImageAt((C2D_Image){&sBattleShadowTex,&shadow},30.25f,20.25f,0,NULL,3.31f,2.73f);C2D_Flush();
            }
            glReadPixels(0,1080-240,400,240,GL_RGBA,GL_UNSIGNED_BYTE,actual);
            if(!mode) memcpy(baseline,actual,400u*240u*4);
            else assert(!memcmp(baseline,actual,400u*240u*4));
            unsigned shades=0;bool seen[256]={0};
            for(unsigned p=0;p<400u*240u;p++) seen[actual[p*4]]=true;
            for(unsigned p=1;p<255;p++) shades+=seen[p];
            assert(shades>10);checks++;
        }
        printf("PASS actual %s pixels unchanged by cached battle Sharp/Smooth choice\n",effect?"bloom":"shadow");
    }
    C3D_RenderTargetDelete(sBloom);sBloom=NULL;C3D_TexDelete(&sBloomTex);free(baseline);free(actual);
}

static void battleSceneChecks(shaderProgram_s *program,C3D_RenderTarget *restore)
{
    C3D_Tex surface={0};assert(C3D_TexInitVRAM(&surface,512,256,GPU_RGBA8));
    sLogical=C3D_RenderTargetCreateFromTex(&surface,GPU_TEXFACE_2D,0,GPU_RB_DEPTH16);
    C3D_RenderTarget *top=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);
    C3D_RenderTarget *canvas=C3D_RenderTargetCreate(1920,1080,GPU_RB_RGBA8,-1);
    assert(sLogical && top && canvas && C3D_TexInit(&fixtureSprite,8,8,GPU_RGBA5551));
    assert(C3D_TexInit(&fixtureHud,32,8,GPU_RGBA5551));
    for(unsigned y=0;y<8;y++) for(unsigned x=0;x<8;x++)
        ((u16 *)fixtureSprite.data)[morton(x,y)]=(x==0||x==7||y==0||y==7||(x>=3&&x<5&&y>=3&&y<5))?0:((x^y)&1)?0xf801:0x07ff;
    for(unsigned y=0;y<8;y++) for(unsigned x=0;x<32;x++)
        ((u16 *)fixtureHud.data)[CtrVideo_Texel(x,y,32)]=((x/2+y)&1)?0xffff:0x0001;
    assert(BattleShadowTexture());
    const unsigned maxScale=(unsigned)voxelScaleCapabilities;
    sBattle=true;sTransition=false;
    for(unsigned voxel=0;voxel<2;voxel++) for(unsigned requested=1;requested<=maxScale;requested++)
    for(unsigned aa=0;aa<=4;aa+=2) {
        unsigned scale=voxel?requested:1,w=240*scale,h=400*scale;
        unsigned char *before=malloc((size_t)w*h*4),*after=malloc((size_t)w*h*4);
        unsigned char *physicalBefore=malloc(1920u*1080u*4);assert(before&&after&&physicalBefore);
        voxelScaleRequested=(int)requested;voxelAaRequested=(int)aa;
        assert(CtrGpu_ConfigureVoxelTargets(sLogical,top,voxel,false)==scale);
        for(unsigned mode=0;mode<3;mode++) {
            imageFilter=mode==1;
            sStats.frames++;ScenePrepare();assert(sScene);
            C3D_RenderTarget *cached=sScene;
            sStats.frames++;ScenePrepare();assert(sScene==cached);
            C2D_Prepare();C2D_TargetClear(sLogical,C2D_Color32(16,32,64,255));C3D_FrameDrawOn(sLogical);
            C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
            if(voxel) {bool active=CtrGpu_BeginVoxelAA(sLogical);aaTriangle(program,-410,1,1,1);if(active)CtrGpu_EndVoxelAA();}
            C2D_Prepare();scaleCompose(sLogical,top,&surface);
            glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_TOP));
            glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,before);
            C3D_FrameDrawOn(canvas);gpuTestDrawScreen(GFX_TOP,(CtrHostRect){0,0,1920,1080},1920,1080,0);
            glReadPixels(0,0,1920,1080,GL_RGBA,GL_UNSIGNED_BYTE,physicalBefore);
            C2D_Prepare();C2D_SceneBegin(sLogical);C2D_ViewReset();
            sZoom=CTR_BATTLE_ZOOM;sOffX=(CTR_GAME_WIDTH-240*sZoom)/2;sOffY=CTR_GAME_HEIGHT-48-112*sZoom;
            sViewX=sViewY=0;sTargetW=400;sTargetH=240;sSurfaceH=256;
            sBattleWorld=voxel;sWorldLayers=voxel?8:0;fixtureStats=mode==2;
            unsigned previous=sceneCompositions;RenderBattleScene(0);assert(sceneCompositions==previous+1);
            /* The real message/stat layer is composed after the scene. Its
             * unscaled native text must stay exact under either scene filter. */
            const Tex3DS_SubTexture hud={32,8,0,1,1,0};
            C2D_DrawImageAt((C2D_Image){&fixtureHud,&hud},80,204,0,NULL,1,1);C2D_Flush();
            scaleCompose(sLogical,top,&surface);
            glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_TOP));
            glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,after);
            battleLcdPalette(after,before,scale,imageFilter,voxel?"voxel":"classic");
            for(unsigned x=80*scale;x<112*scale;x++) for(unsigned y=204*scale;y<212*scale;y++) {
                unsigned color=paletteIndex(after+((size_t)x*w+(w-1-y))*4);assert(color==2||color==3);
            }
            checks++;battlePhysicalPalette(canvas,physicalBefore,imageFilter,scale,voxel,aa);
            assert((sSceneTex.param&6)==(imageFilter?6:0));
            assert((sBattleShadowTex.param&6)==6);checks++;
        }
        printf("PASS actual battle scene %.2f/%.2f %s request%ux AA%u: Sharp/Smooth/Sharp, cached FBO, colors/alpha holes/HUD and 1920x1080 panel\n",
               (double)CTR_BATTLE_ZOOM,(double)SCENE_ZOOM,voxel?"voxel":"classic",requested,aa);
        free(before);free(after);free(physicalBefore);
    }
    /* Released targets use the latest choice on reallocation too. */
    for(int filter=0;filter<2;filter++) {
        sBattle=false;sTransition=false;sStats.frames+=LAYER_IDLE_FRAMES+1;ScenePrepare();assert(!sScene);
        imageFilter=filter;sBattle=true;ScenePrepare();assert(sScene && (sSceneTex.param&6)==(filter?6:0));checks++;
    }
    SceneRelease();sSceneFailed=false;imageFilter=1;failNextVramAllocation=true;
    ScenePrepare();assert(!sScene && sSceneFailed && !failNextVramAllocation);
    imageFilter=0;sStats.frames=sSceneFailFrame+LAYER_RETRY_FRAMES-1;ScenePrepare();assert(!sScene);
    sStats.frames++;ScenePrepare();assert(sScene && (sSceneTex.param&6)==0);checks++;
    /* TransitionPicture shares sSceneTex and must follow cached filter
     * changes, including the two half-image seam and stretched margins. */
    sBattle=false;sTransition=true;voxelScaleRequested=1;
    CtrGpu_ConfigureVoxelTargets(sLogical,top,false,false);
    for(int mode=0;mode<3;mode++) {
        imageFilter=mode==1;ScenePrepare();
        C2D_Prepare();C2D_SceneBegin(sScene);C2D_TargetClear(sScene,C2D_Color32(255,255,255,255));
        for(unsigned half=0;half<2;half++) for(unsigned x=0;x<240;x++)
            C2D_DrawRectSolid(half*TRANSITION_HALF_X+x*SCENE_ZOOM,0,0,SCENE_ZOOM,SCENE_H,
                             (x&1)?C2D_Color32(0,0,0,255):C2D_Color32(255,255,255,255));
        C2D_Flush();GpuSplit();C2D_SceneBegin(top);C2D_ViewReset();
        TransitionPicture();C2D_Flush();gpuTransferToScreen(gpuFindTarget(&top->frameBuf),GFX_TOP,240,400);
        glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_TOP));
        unsigned char rows[240*400*4];glReadPixels(0,0,240,400,GL_RGBA,GL_UNSIGNED_BYTE,rows);
        unsigned black=0,white=0,mixed=0;
        for(unsigned x=0;x<400;x++)for(unsigned y=0;y<240;y++) {
            const unsigned char *p=rows+((size_t)x*240+239-y)*4;
            assert(p[0]==p[1]&&p[1]==p[2]&&p[3]==255);
            if(x<20) assert(p[0]==255);
            if(x>=380) assert(p[0]==0);
            if(p[0]==0)black++;else if(p[0]==255)white++;else mixed++;
        }
        assert(black&&white&&((mixed>0)==(imageFilter!=0)));checks++;
    }
    battleEffectsPreserved(canvas,&surface);
    SceneRelease();C3D_TexDelete(&sBattleShadowTex);C3D_TexDelete(&fixtureSprite);C3D_TexDelete(&fixtureHud);
    C3D_RenderTargetDelete(sLogical);C3D_RenderTargetDelete(top);C3D_RenderTargetDelete(canvas);C3D_TexDelete(&surface);
    imageFilter=0;voxelAaRequested=0;voxelScaleRequested=1;
    C2D_Prepare();C2D_SceneBegin(restore);C2D_ViewReset();assert(glGetError()==GL_NO_ERROR);
}
