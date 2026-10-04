/* Included by offscreen.c: real translated voxel triangles, GLES resolves and
 * 2D textures. Host configuration is supplied by that test's existing stubs. */
#include <ctr_gpu_voxel.h>
extern unsigned gpuTestVoxelAaSamples(void);
extern unsigned gpuTestVoxelAaAllocations(void);
extern GLuint gpuTestVoxelAaFramebuffer(void);
extern void gpuTestVoxelAaFailSamples(unsigned mask);
static int expectedVoxelAaCapabilities;

static void aaTriangle(shaderProgram_s *program, short z, unsigned r, unsigned g, unsigned b)
{
    static struct { float u,v; short x,y,z,shade; } vertices[3];
    vertices[0]=(typeof(*vertices)){0,0,-400,-390,z,8192};
    vertices[1]=(typeof(*vertices)){0,0,410,-390,z,8192};
    vertices[2]=(typeof(*vertices)){0,0,-400,405,z,8192};
    C3D_BindProgram(program);
    C3D_Mtx identity; Mtx_Identity(&identity);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,shaderInstanceGetUniformLocation(program->vertexShader,"projection"),&identity);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,shaderInstanceGetUniformLocation(program->vertexShader,"modelView"),&identity);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,shaderInstanceGetUniformLocation(program->vertexShader,"shadeTint"),2*r,2*g,2*b,0);
    for(int i=0;i<6;i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvSrc(C3D_GetTexEnv(0),C3D_Both,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_AlphaTest(false,GPU_ALWAYS,0); C3D_ColorLogicOp(GPU_LOGICOP_COPY);
    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
    AttrInfo_Init(C3D_GetAttrInfo());
    AttrInfo_AddLoader(C3D_GetAttrInfo(),1,GPU_FLOAT,2);
    AttrInfo_AddLoader(C3D_GetAttrInfo(),0,GPU_SHORT,4);
    BufInfo_Init(C3D_GetBufInfo()); BufInfo_Add(C3D_GetBufInfo(),vertices,sizeof(*vertices),2,0x10);
    C3D_DrawArrays(GPU_TRIANGLES,0,3);
}

static unsigned aaRender(shaderProgram_s *program,C3D_RenderTarget *target,unsigned samples)
{
    unsigned w=target->frameBuf.width,h=target->frameBuf.height;
    unsigned char pixels[64*64*4]; assert(w<=64 && h<=64);
    C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
    C2D_TargetClear(target,C2D_Color32(0,0,0,255)); C3D_FrameDrawOn(target);
    glDepthRangef(0,1);
    voxelAaRequested=(int)samples;
    bool active=CtrGpu_BeginVoxelAA(target);
    unsigned expected=samples>=4 && (voxelAaCapabilities&2)?4:samples>=2 && (voxelAaCapabilities&1)?2:0;
    assert(active==(expected!=0) && gpuTestVoxelAaSamples()==expected);
    aaTriangle(program,-410,1,1,1);
    if(active) CtrGpu_EndVoxelAA();
    assert(gpuTestVoxelAaSamples()==0 && C3D_GetFrameBuf()==&target->frameBuf);
    assert(glGetError()==GL_NO_ERROR);
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    unsigned partial=0;
    for(unsigned i=0;i<w*h;i++) {
        assert(pixels[4*i]==pixels[4*i+1] && pixels[4*i]==pixels[4*i+2]);
        if(pixels[4*i]>2 && pixels[4*i]<253) partial++;
    }
    assert((partial>0)==active); checks++;
    /* Resolving color alone would allow this farther sprite triangle to
     * overwrite the terrain; resolving depth must retain occlusion. */
    aaTriangle(program,-100,0,0,1);
    pixel(w/3,h/3,255,255,255,255);
    aaTriangle(program,-490,0,0,1);
    pixel(w/3,h/3,0,0,255,255);
    pixel(0,0,0,0,0,255); // unchanged clear outside all geometry
    return partial;
}

static void voxelAaChecks(shaderProgram_s *program,C3D_RenderTarget *restore)
{
    int capabilities=voxelAaCapabilities;
    expectedVoxelAaCapabilities=capabilities;
    assert(capabilities>=0 && capabilities<=3);
    printf("VOXEL MSAA capabilities: 2x=%d 4x=%d\n",!!(capabilities&1),!!(capabilities&2));
    C3D_RenderTarget *target=C3D_RenderTargetCreate(64,64,GPU_RB_RGBA8,GPU_RB_DEPTH16);
    C3D_RenderTarget *small=C3D_RenderTargetCreate(48,32,GPU_RB_RGBA8,GPU_RB_DEPTH16);
    assert(target && small);
    C3D_Tex art={0}; assert(C3D_TexInit(&art,8,8,GPU_RGBA5551));
    for(unsigned y=0;y<8;y++) for(unsigned x=0;x<8;x++)
        ((u16 *)art.data)[morton(x,y)]=((x+y)&1)?0xF801:0x07FF;
    Tex3DS_SubTexture sub={8,8,0,1,1,0};
    for(unsigned samples=0;samples<=4;samples+=2) {
        unsigned partial=aaRender(program,target,samples);
        unsigned allocations=gpuTestVoxelAaAllocations();
        assert(aaRender(program,target,samples)==partial);
        assert(gpuTestVoxelAaAllocations()==allocations); checks++; // reuse every warm frame
        C2D_Prepare(); C2D_SceneBegin(target); C2D_ViewReset();
        C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
        C2D_DrawImageAt((C2D_Image){&art,&sub},40,0,0,NULL,1,1);
        /* Pixel art is still exact red/cyan, including at the outer edges. */
        pixel(40,63,0,255,255,255); pixel(41,63,255,0,0,255);
        pixel(40,62,255,0,0,255); pixel(47,56,0,255,255,255);
        printf("VOXEL MSAA request=%u partial-edge-pixels=%u\n",samples,partial);
    }
    aaRender(program,small,4); // reallocate for different dimensions, same depth/color contract
    GLuint old=gpuTestVoxelAaFramebuffer();
    gpuVoxelAaShutdown();
    assert(!gpuTestVoxelAaFramebuffer() && (!old || !glIsFramebuffer(old))); checks++;
    gpuVoxelAaInit(); assert(voxelAaCapabilities==capabilities);
    aaRender(program,target,4);

    /* Allocation failure must leave the original target/clear usable and
     * publish the fallback. An unsupported 2x choice must never become4x. */
    gpuVoxelAaShutdown(); gpuVoxelAaInit();
    gpuTestVoxelAaFailSamples(3);
    aaRender(program,target,4);
    assert(voxelAaCapabilities==0 && !gpuTestVoxelAaFramebuffer()); checks++;
    gpuVoxelAaInit();
    assert(voxelAaCapabilities==0); checks++; // failure fallback survives duplicate init
    gpuTestVoxelAaFailSamples(0); gpuVoxelAaShutdown(); gpuVoxelAaInit();
    if(capabilities==3) {
        gpuTestVoxelAaFailSamples(2); aaRender(program,target,4);
        assert(voxelAaCapabilities==1); checks++;
        gpuTestVoxelAaFailSamples(0); gpuVoxelAaShutdown(); gpuVoxelAaInit();
    }
    voxelAaRequested=0;
    /* An ordinary classic/2D target is not redirected even if the preference
     * is on: the scoped calls are only made by CtrVoxel_Draw. */
    C2D_Prepare(); C2D_SceneBegin(target); C2D_ViewReset();
    voxelAaRequested=4;
    C2D_DrawRectSolid(0,0,0,64,64,C2D_Color32(255,0,255,255));
    pixel(0,0,255,0,255,255); pixel(63,63,255,0,255,255);
    assert(!gpuTestVoxelAaSamples()); checks++;
    voxelAaRequested=0;
    C3D_TexDelete(&art); C3D_RenderTargetDelete(small); C3D_RenderTargetDelete(target);
    C2D_Prepare(); C2D_SceneBegin(restore); C2D_ViewReset();
    assert(glGetError()==GL_NO_ERROR);
}

/* Called after the ordinary test's C3D_Fini but before gfxExit: the EGL
 * context is still alive, so gpuInit's fast path must not skip AA recovery. */
static void voxelAaReinitChecks(shaderProgram_s *program)
{
    assert(voxelAaCapabilities==-1 && !gpuTestVoxelAaFramebuffer());
    assert(C3D_Init(0) && C2D_Init(128)); C2D_Prepare();
    assert(voxelAaCapabilities==expectedVoxelAaCapabilities); checks++;
    C3D_RenderTarget *target=C3D_RenderTargetCreate(48,32,GPU_RB_RGBA8,GPU_RB_DEPTH16);
    assert(target);
    aaRender(program,target,4);
    voxelAaRequested=0;
    C3D_RenderTargetDelete(target);
    C2D_Fini(); C3D_Fini();
    assert(voxelAaCapabilities==-1 && !gpuTestVoxelAaFramebuffer()); checks++;
    assert(glGetError()==GL_NO_ERROR);
}
