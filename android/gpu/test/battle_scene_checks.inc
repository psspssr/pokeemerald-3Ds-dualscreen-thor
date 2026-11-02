#include <ctr_gpu_battle.h>
static C3D_Tex sSceneTex;
#include "upstream_battle_scene.inc"

static void battleSceneChecks(shaderProgram_s *program,C3D_RenderTarget *restore)
{
    (void)program;
    assert(C3D_TexInit(&sSceneTex,8,8,GPU_RGBA5551));
    for(unsigned y=0;y<8;y++) for(unsigned x=0;x<8;x++)
        ((u16 *)sSceneTex.data)[morton(x,y)]=x<4?0xf801:0x003f;
    C3D_RenderTarget *target=C3D_RenderTargetCreate(32,32,GPU_RB_RGBA8,GPU_RB_DEPTH16);
    assert(target);
    C2D_Prepare();C2D_SceneBegin(target);C2D_ViewReset();
    const Tex3DS_SubTexture sub={8,8,0,1,1,0};
    imageFilter=0;SceneSetFilter();SceneTexFilter(GPU_LINEAR);
    C2D_DrawImageAt((C2D_Image){&sSceneTex,&sub},0,0,0,NULL,4,4);
    pixel(15,16,255,0,0,255);pixel(16,16,0,0,255,255);
    imageFilter=1;SceneSetFilter();SceneTexFilter(GPU_LINEAR);
    C2D_DrawImageAt((C2D_Image){&sSceneTex,&sub},0,0,0,NULL,4,4);C2D_Flush();
    unsigned char p[4];glReadPixels(15,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    assert(p[0]>0 && p[0]<255 && p[2]>0 && p[2]<255);checks++;
    imageFilter=0;
    /* The new badges use all three primitives, including color interpolation. */
    C2D_TargetClear(target,C2D_Color32(0,0,0,255));
    assert(C2D_DrawTriangle(0,0,0xff0000ff,32,0,0xff0000ff,0,32,0xff0000ff,0));
    pixel(2,29,255,0,0,255);
    assert(C2D_DrawLine(4,16,0xff00ff00,28,16,0xff00ff00,4,0));
    pixel(16,16,0,255,0,255);
    assert(C2D_DrawCircleSolid(24,24,0,5,0xffff0000));
    pixel(24,8,0,0,255,255);
    /* GPU -> linear -> GPU must preserve the picture, not stale CPU bytes. */
    u32 *saved=malloc(32*32*4);assert(saved);
    C3D_SyncTextureCopy(target->frameBuf.colorBuf,0,saved,0,32*32*4,0);
    C2D_TargetClear(target,C2D_Color32(255,255,255,255));pixel(24,8,255,255,255,255);
    C3D_SyncTextureCopy(saved,0,target->frameBuf.colorBuf,0,32*32*4,0);
    pixel(24,8,0,0,255,255);pixel(16,16,0,255,0,255);pixel(2,29,255,0,0,255);
    free(saved);C3D_TexDelete(&sSceneTex);C3D_RenderTargetDelete(target);
    C2D_Prepare();C2D_SceneBegin(restore);C2D_ViewReset();
    puts("PASS upstream battle filter restore, badge primitives and held-Pokedex framebuffer round trip");
}
