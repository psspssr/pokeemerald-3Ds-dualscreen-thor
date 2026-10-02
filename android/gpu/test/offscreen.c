/* Executed on an Android emulator/device with its real GLES implementation.
 * No game code is stubbed: this is an isolated renderer conformance harness. */
#include "../src/gpu_internal.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <ctrshim_apt.h>

static CtrHostState state=CTR_HOST_RUNNING;
static unsigned gameSpeed=1;
/* Link wrappers count actual backend/driver work without adding counters to
 * the production renderer. Pixel checks below still use the real GLES API. */
static unsigned uniformCalls,uniformVectors,textureBinds,attributeCalls;
void __real_glUniform4fv(GLint location,GLsizei count,const GLfloat *value);
void __wrap_glUniform4fv(GLint location,GLsizei count,const GLfloat *value)
{ uniformCalls++; uniformVectors+=(unsigned)count; __real_glUniform4fv(location,count,value); }
void __real_glBindTexture(GLenum target,GLuint texture);
void __wrap_glBindTexture(GLenum target,GLuint texture)
{ textureBinds++; __real_glBindTexture(target,texture); }
void __real_glDisableVertexAttribArray(GLuint index);
void __wrap_glDisableVertexAttribArray(GLuint index)
{ attributeCalls++; __real_glDisableVertexAttribArray(index); }
void __real_glVertexAttribPointer(GLuint index,GLint size,GLenum type,GLboolean normalized,GLsizei stride,const void *pointer);
void __wrap_glVertexAttribPointer(GLuint index,GLint size,GLenum type,GLboolean normalized,GLsizei stride,const void *pointer)
{ attributeCalls++; __real_glVertexAttribPointer(index,size,type,normalized,stride,pointer); }
extern GLuint gpuTestScreenFramebuffer(gfxScreen_t screen);
extern double gpuTestPacingDeadline(void);
extern unsigned gpuTestPresentCount(void);
static CtrAptListener gpuListener;
bool CtrApt_AddListener(CtrAptListener listener,void *user) { (void)user;gpuListener=listener;return true; }
void CtrApt_RemoveListener(CtrAptListener listener,void *user) { (void)listener;(void)user;gpuListener=NULL; }
void *linearAlloc(size_t size) { return calloc(1,size); }
void *vramAlloc(size_t size) { return calloc(1,size); }
void linearFree(void *ptr) { free(ptr); }
void vramFree(void *ptr) { free(ptr); }
void CtrMem_Register(CtrMemKind k,void *p,size_t s,void *o) { (void)k;(void)p;(void)s;(void)o; }
void CtrMem_Unregister(void *p) { (void)p; }
bool CtrMem_Find(const void *p,CtrMemBlock *out) { (void)p;(void)out;return false; }
void CtrMem_SetOwner(void *p,void *owner) { (void)p;(void)owner; }
void CtrHost_GetLayout(CtrHostLayout *layout) { memset(layout,0,sizeof(*layout)); }
CtrHostState CtrHost_GetState(void) { return state; }
unsigned CtrHost_GameSpeed(void) { return gameSpeed; }
void CtrHost_SetState(CtrHostState s) { state=s; }
uint32_t CtrHost_WindowGenerationAt(int index) { (void)index;return 0; }
struct ANativeWindow *CtrHost_AcquireWindowAt(int index,uint32_t *generation) { (void)index;*generation=0;return NULL; }

static int checks;
static void pixel(unsigned x,unsigned y,unsigned r,unsigned g,unsigned b,unsigned a)
{
    unsigned char p[4];
    C2D_Flush(); assert(glGetError()==GL_NO_ERROR); glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    if(abs((int)p[0]-(int)r)>2 || abs((int)p[1]-(int)g)>2 || abs((int)p[2]-(int)b)>2 || abs((int)p[3]-(int)a)>2) {
        fprintf(stderr,"FAIL pixel(%u,%u) got %u,%u,%u,%u expected %u,%u,%u,%u\n",x,y,p[0],p[1],p[2],p[3],r,g,b,a); abort();
    }
    assert(glGetError()==GL_NO_ERROR); checks++;
}
static unsigned morton(unsigned x,unsigned y)
{ return (x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3); }
static double callerCpuTime(void)
{ struct timespec now; assert(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&now)==0); return now.tv_sec+now.tv_nsec*1e-9; }

int main(int argc,char **argv)
{
    assert(argc==2 || (argc==3 && !strcmp(argv[2],"--benchmark")));
    assert(C3D_Init(0)); assert(C2D_Init(128)); C2D_Prepare();
    /* A byte sentinel cannot represent "not uploaded": all-white CPU data
     * must initialize the LCD even if it equals that sentinel byte-for-byte. */
    u8 *topLcd=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);
    memset(topLcd,0xff,400*240*3); gpuFlushScreens();
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_TOP));
    pixel(10,10,255,255,255,255);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    gfxSetScreenFormat(GFX_TOP,GSP_RGB565_OES);
    memset(topLcd,0xff,400*240*2); gpuFlushScreens();
    pixel(10,10,255,255,255,255);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    memset(topLcd,0xff,400*240*2); gfxFlushBuffers(); gpuFlushScreens();
    pixel(10,10,255,255,255,255);
    C3D_RenderTarget *target=C3D_RenderTargetCreate(16,16,GPU_RB_RGBA8,GPU_RB_DEPTH16); assert(target);
    C2D_TargetClear(target,C2D_Color32(0,0,0,255)); C2D_SceneBegin(target);
    C2D_DrawRectSolid(0,0,0,8,8,C2D_Color32(255,0,0,255));
    C2D_DrawRectSolid(8,0,0,8,8,C2D_Color32(0,255,0,255));
    C2D_DrawRectSolid(0,8,0,8,8,C2D_Color32(0,0,255,255));
    pixel(2,13,255,0,0,255); pixel(13,13,0,255,0,255); pixel(2,2,0,0,255,255);
    C2D_ViewReset();
    C2D_DrawRectSolid(0,0,0,8,16,C2D_Color32(255,0,0,255));
    C2D_ViewTranslate(8,0);
    C2D_DrawRectSolid(0,0,0,8,16,C2D_Color32(0,255,0,255));
    C2D_ViewReset();
    pixel(2,2,255,0,0,255); pixel(13,2,0,255,0,255);
    C3D_Tex texture={0}; assert(C3D_TexInit(&texture,8,8,GPU_RGBA5551));
    for(unsigned y=0;y<8;y++) for(unsigned x=0;x<8;x++) ((u16 *)texture.data)[morton(x,y)]=y<4?(x<4?0xf801:0x07c1):(x<4?0x003f:0xffff);
    Tex3DS_SubTexture sub={8,8,0,1,1,0}; C2D_Image image={&texture,&sub};
    C2D_DrawImageAt(image,0,0,0,NULL,2,2);
    pixel(2,13,255,0,0,255); pixel(13,13,0,255,0,255); pixel(2,2,0,0,255,255);
    C2D_DrawImageAt(image,0,0,0,NULL,-2,2); pixel(2,13,0,255,0,255); pixel(13,13,255,0,0,255);
    C2D_ImageTint tint; C2D_PlainImageTint(&tint,C2D_Color32(0,0,0,255),0.5f);
    C2D_DrawImageAt(image,0,0,0,&tint,2,2); pixel(2,13,128,0,0,255);
    /* CPU edits without a flush must invalidate the shadow before the next draw. */
    for(unsigned i=0;i<64;i++) ((u16 *)texture.data)[i]=0x07c1;
    C2D_DrawImageAt(image,0,0,0,NULL,2,2); pixel(2,13,0,255,0,255);
    C3D_TexEnv *effect=C3D_GetTexEnv(4);
    C3D_TexEnvSrc(effect,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_CONSTANT);
    C3D_TexEnvFunc(effect,C3D_RGB,GPU_SUBTRACT);
    C3D_TexEnvColor(effect,C2D_Color32(0,128,0,255));
    C2D_DrawImageAt(image,0,0,0,NULL,2,2); pixel(2,13,0,127,0,255);
    C3D_TexEnvColor(C3D_GetTexEnv(4),C2D_Color32(0,64,0,255));
    C2D_DrawImageAt(image,0,0,0,NULL,2,2); pixel(2,13,0,191,0,255);
    C3D_TexEnvInit(C3D_GetTexEnv(4));
    C2D_DrawRectSolid(0,0,0,16,16,C2D_Color32(255,0,0,255));
    for(unsigned i=0;i<64;i++) ((u16 *)texture.data)[i]=0;
    C2D_DrawImageAt(image,0,0,0,NULL,2,2); pixel(2,13,255,0,0,255);
    C3D_SetScissor(GPU_SCISSOR_NORMAL,0,8,8,16);
    C2D_DrawRectSolid(0,0,0,16,16,C2D_Color32(0,255,0,255));
    pixel(2,13,0,255,0,255); pixel(13,13,255,0,0,255);
    C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
    C2D_DrawRectSolid(0,0,0,16,16,C2D_Color32(255,0,0,255));
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_CONSTANT_COLOR,GPU_CONSTANT_ALPHA,GPU_ONE,GPU_ZERO);
    C3D_BlendingColor(C2D_Color32(128,128,128,64));
    C2D_DrawRectSolid(0,0,0,16,16,C2D_Color32(0,0,255,255)); pixel(2,13,64,0,128,255);
    C2D_Prepare();

    C3D_Tex rowsTexture={0}; assert(C3D_TexInit(&rowsTexture,16,16,GPU_RGBA5551));
    for(unsigned i=0;i<256;i++) ((u16 *)rowsTexture.data)[i]=0xF801;
    Tex3DS_SubTexture rowsSub={16,16,0,1,1,0};
    C2D_DrawImageAt((C2D_Image){&rowsTexture,&rowsSub},0,0,0,NULL,1,1); pixel(2,13,255,0,0,255);
    /* Change only the second PICA tile row. Its GL y-origin is zero. */
    for(unsigned i=128;i<256;i++) ((u16 *)rowsTexture.data)[i]=0x07C1;
    C2D_DrawImageAt((C2D_Image){&rowsTexture,&rowsSub},0,0,0,NULL,1,1);
    pixel(2,13,255,0,0,255); pixel(2,2,0,255,0,255);
    for(unsigned i=0;i<128;i++) ((u16 *)rowsTexture.data)[i]=0x003F;
    C2D_DrawImageAt((C2D_Image){&rowsTexture,&rowsSub},0,0,0,NULL,1,1);
    pixel(2,13,0,0,255,255); pixel(2,2,0,255,0,255);

    /* Building pages use PageTexInit: a hand-built descriptor pointing inside
     * a shared VRAM arena. They never call TexInit or TexDelete. */
    u16 *arena=vramAlloc(2048); assert(arena); arena[0]=0x5aa5;
    C3D_Tex manual={.data=arena+64,.fmt=GPU_RGBA5551,.size=16*16*2,.width=16,.height=16};
    for(unsigned i=0;i<256;i++) ((u16 *)manual.data)[i]=0xF83F;
    Tex3DS_SubTexture manualSub={16,16,0,1,1,0};
    C2D_DrawImageAt((C2D_Image){&manual,&manualSub},0,0,0,NULL,1,1);
    pixel(2,13,255,0,255,255); pixel(2,2,255,0,255,255);
    /* Reuse the same descriptor with a different slice and dimensions. */
    memset(&manual,0,sizeof(manual));
    manual=(C3D_Tex){.data=arena+384,.fmt=GPU_RGBA5551,.size=16*8*2,.width=16,.height=8};
    for(unsigned i=0;i<128;i++) ((u16 *)manual.data)[i]=0x07C1;
    manualSub=(Tex3DS_SubTexture){16,8,0,1,1,0};
    C2D_DrawImageAt((C2D_Image){&manual,&manualSub},0,0,0,NULL,1,2);
    pixel(2,13,0,255,0,255); pixel(2,2,0,255,0,255);
    /* A format change must also invalidate the GL storage and CPU shadow. */
    manual=(C3D_Tex){.data=arena+576,.fmt=GPU_RGB565,.size=8*16*2,.width=8,.height=16};
    for(unsigned i=0;i<128;i++) ((u16 *)manual.data)[i]=0xF800;
    manualSub=(Tex3DS_SubTexture){8,16,0,1,1,0};
    C2D_DrawImageAt((C2D_Image){&manual,&manualSub},0,0,0,NULL,2,1);
    pixel(2,13,255,0,0,255); pixel(2,2,255,0,0,255);
    manual.fmt=GPU_RGBA5551;
    for(unsigned i=0;i<128;i++) ((u16 *)manual.data)[i]=0x003F;
    C2D_DrawImageAt((C2D_Image){&manual,&manualSub},0,0,0,NULL,2,1);
    pixel(2,13,0,0,255,255);
    assert(arena[0]==0x5aa5);
    /* Match origin shutdown: release the whole arena and clear descriptors,
     * then let C3D_Fini delete GPU records. It must not free an arena slice. */
    vramFree(arena); memset(&manual,0,sizeof(manual));

    C3D_Tex renderTexture={0}; assert(C3D_TexInitVRAM(&renderTexture,16,16,GPU_RGBA8));
    C3D_RenderTarget *renderTarget=C3D_RenderTargetCreateFromTex(&renderTexture,GPU_TEXFACE_2D,0,-1); assert(renderTarget);
    C2D_SceneBegin(renderTarget); C2D_DrawRectSolid(0,0,0,16,8,C2D_Color32(255,0,255,255));
    C2D_DrawRectSolid(0,8,0,16,8,C2D_Color32(0,255,255,255));
    C2D_SceneBegin(target); sub=(Tex3DS_SubTexture){16,16,0,1,1,0};
    C2D_DrawImageAt((C2D_Image){&renderTexture,&sub},0,0,0,NULL,1,1);
    pixel(3,13,255,0,255,255); pixel(3,2,0,255,255,255);

    /* The top-left of a rotated screen maps to framebuffer x=height-1,y=0. */
    C2D_SceneSize(16,16,true); C2D_DrawRectSolid(0,0,0,8,8,C2D_Color32(255,255,0,255));
    pixel(13,2,255,255,0,255);
    C2D_SceneSize(16,16,false); C2D_TargetClear(target,0);
    C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
    C2D_DrawRectSolid(0,0,-0.5f,16,16,C2D_Color32(0,255,0,255)); pixel(2,13,0,255,0,255);
    C2D_DrawRectSolid(0,0,0.75f,16,16,C2D_Color32(0,0,255,255)); pixel(2,13,0,0,255,255);
    C2D_DrawRectSolid(0,0,-0.75f,16,16,C2D_Color32(255,0,0,255)); pixel(2,13,0,0,255,255);

    FILE *file=fopen(argv[1],"rb"); assert(file); fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
    u32 *shader=malloc(size); assert(fread(shader,1,size,file)==(size_t)size); fclose(file);
    DVLB_s *binary=DVLB_ParseFile(shader,size); assert(binary); free(shader);
    shaderProgram_s program; assert(shaderProgramInit(&program)==0); assert(shaderProgramSetVsh(&program,&binary->DVLE[0])==0);
    C3D_BindProgram(&program); C3D_Mtx identity; Mtx_Identity(&identity);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,shaderInstanceGetUniformLocation(program.vertexShader,"projection"),&identity);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,shaderInstanceGetUniformLocation(program.vertexShader,"modelView"),&identity);
    int shade=shaderInstanceGetUniformLocation(program.vertexShader,"shadeTint");
    C3D_FVUnifSet(GPU_VERTEX_SHADER,shade,2,0,0,0);
    for(int i=0;i<6;i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvSrc(C3D_GetTexEnv(0),C3D_Both,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_AlphaTest(false,GPU_ALWAYS,0); C3D_ColorLogicOp(GPU_LOGICOP_COPY); C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
    C2D_TargetClear(target,0); C3D_FrameDrawOn(target);
    struct { float u,v; short x,y,z,shade; } triangle[3]={{0,0,-384,-384,-410,8192},{0,0,384,-384,-410,8192},{0,0,0,384,-410,8192}};
    AttrInfo_Init(C3D_GetAttrInfo()); AttrInfo_AddLoader(C3D_GetAttrInfo(),1,GPU_FLOAT,2); AttrInfo_AddLoader(C3D_GetAttrInfo(),0,GPU_SHORT,4);
    BufInfo_Init(C3D_GetBufInfo()); BufInfo_Add(C3D_GetBufInfo(),triangle,sizeof(*triangle),2,0x10);
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,255,0,0,255);
    for(int i=0;i<3;i++) triangle[i].z=-100;
    C3D_FVUnifSet(GPU_VERTEX_SHADER,shade,0,0,2,0);
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,255,0,0,255);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR); C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,0,255,255);
    /* Uniform values belong to a linked program. Reusing a cached program
     * after another variant must see current values, including direct writes
     * through Citro3D's public register array without dirty flags. */
    C3D_FVUnifSet(GPU_VERTEX_SHADER,shade,0,2,0,0);
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,255,0,255);
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,255,0,255);
    C3D_AlphaTest(true,GPU_GEQUAL,1);
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,255,0,255);
    C3D_FVUnif[0][shade].x=2; C3D_FVUnif[0][shade].y=0;
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,255,0,0,255);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,255,0,0,255);
    C3D_FVUnif[0][shade].x=0; C3D_FVUnif[0][shade].z=2;
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,0,255,255);
    C2D_TargetClear(target,C2D_Color32(0,0,0,255));
    C3D_AlphaTest(true,GPU_GREATER,255);
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,0,0,255);
    C3D_AlphaTest(true,GPU_GREATER,254);
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,0,255,255);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    /* Include the third combiner operand and a shader using only sampler2;
     * inactive-unit pruning must never remove a texture that contributes. */
    C3D_Tex operands[3]={{0}};
    const u16 operandColors[]={0xF801,0x07C1,0x003F};
    for(int unit=0;unit<3;unit++) {
        assert(C3D_TexInit(&operands[unit],8,8,GPU_RGBA5551));
        for(unsigned i=0;i<64;i++) ((u16 *)operands[unit].data)[i]=operandColors[unit];
        C3D_TexBind(unit,&operands[unit]);
    }
    C3D_TexEnv *operandEnv=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(operandEnv,C3D_Both,GPU_TEXTURE0,GPU_TEXTURE1,GPU_TEXTURE2);
    C3D_TexEnvFunc(operandEnv,C3D_Both,GPU_INTERPOLATE);
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,255,0,255);
    C3D_TexEnvInit(C3D_GetTexEnv(0));
    C3D_TexEnvSrc(C3D_GetTexEnv(0),C3D_Both,GPU_TEXTURE2,GPU_PREVIOUS,GPU_PREVIOUS);
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,0,255,255);
    for(int unit=0;unit<3;unit++) C3D_TexDelete(&operands[unit]);
    C3D_TexEnvInit(C3D_GetTexEnv(0));
    C3D_TexEnvSrc(C3D_GetTexEnv(0),C3D_Both,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    /* A VAO can keep its layout while data/first/count change, but must
     * reconfigure when loader order, stride or the number of buffers changes. */
    struct { short x,y,z,shade; float u,v; u32 pad; } alternate[6];
    for(int i=0;i<6;i++) {
        alternate[i].x=triangle[i%3].x+(i>=3?4096:0);
        alternate[i].y=triangle[i%3].y; alternate[i].z=triangle[i%3].z;
        alternate[i].shade=triangle[i%3].shade; alternate[i].u=alternate[i].v=0; alternate[i].pad=0;
    }
    AttrInfo_Init(C3D_GetAttrInfo()); AttrInfo_AddLoader(C3D_GetAttrInfo(),0,GPU_SHORT,4); AttrInfo_AddLoader(C3D_GetAttrInfo(),1,GPU_FLOAT,2);
    BufInfo_Init(C3D_GetBufInfo()); BufInfo_Add(C3D_GetBufInfo(),alternate,sizeof(*alternate),2,0x10);
    C2D_TargetClear(target,C2D_Color32(0,0,0,255));
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,0,255,255);
    C2D_TargetClear(target,C2D_Color32(0,0,0,255));
    C3D_DrawArrays(GPU_TRIANGLES,3,3); pixel(8,8,0,0,0,255);
    C3D_GetBufInfo()->buffers[0].data=alternate+3;
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,0,0,255);
    C3D_GetBufInfo()->buffers[0].data=alternate;
    C2D_DrawRectSolid(0,0,0,16,16,C2D_Color32(255,0,0,255)); C2D_Flush();
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,0,255,255);
    float uv[3][2]={{0}}; short positions[3][4];
    for(int i=0;i<3;i++) { positions[i][0]=triangle[i].x; positions[i][1]=triangle[i].y; positions[i][2]=triangle[i].z; positions[i][3]=triangle[i].shade; }
    AttrInfo_Init(C3D_GetAttrInfo()); AttrInfo_AddLoader(C3D_GetAttrInfo(),1,GPU_FLOAT,2); AttrInfo_AddLoader(C3D_GetAttrInfo(),0,GPU_SHORT,4);
    BufInfo_Init(C3D_GetBufInfo()); BufInfo_Add(C3D_GetBufInfo(),uv,sizeof(*uv),1,0); BufInfo_Add(C3D_GetBufInfo(),positions,sizeof(*positions),1,1);
    C2D_TargetClear(target,C2D_Color32(0,0,0,255));
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,0,255,255);
    BufInfo_Init(C3D_GetBufInfo()); BufInfo_Add(C3D_GetBufInfo(),triangle,sizeof(*triangle),2,0x10);
    C2D_TargetClear(target,C2D_Color32(0,0,0,255));
    C3D_DrawArrays(GPU_TRIANGLES,0,3); pixel(8,8,0,0,255,255);
    /* The depth-plane pass relies on the RGB write mask preserving coverage. */
    C3D_Tex maskTexture={0}; assert(C3D_TexInitVRAM(&maskTexture,16,16,GPU_RGBA5551));
    C3D_RenderTarget *mask=C3D_RenderTargetCreateFromTex(&maskTexture,GPU_TEXFACE_2D,0,-1); assert(mask);
    C2D_Prepare(); C2D_SceneBegin(mask); C2D_ViewReset();
    GLint alphaBits; glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE,&alphaBits); assert(alphaBits==1);
    C2D_TargetClear(mask,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    C2D_DrawRectSolid(0,0,0,8,8,C2D_Color32(255,0,255,255)); pixel(2,13,255,0,255,255);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_RED|GPU_WRITE_GREEN|GPU_WRITE_BLUE);
    C2D_DrawRectSolid(0,0,0,16,16,C2D_Color32(255,0,255,200));
    pixel(2,13,255,0,255,255); pixel(2,2,255,0,255,0);
    assert(gpuListener); gpuListener(CTR_APT_SUSPEND,NULL); assert(gpuTestPacingDeadline()==0);
    double resumedBefore=gpuNow(); gpuListener(CTR_APT_RESUME,NULL);
    assert(gpuTestPacingDeadline()>=resumedBefore && gpuTestPacingDeadline()<=gpuNow());
    pixel(2,2,255,0,255,0);

    /* The bottom menu copies GPU columns over a CPU canvas, leaving the
     * right-side touchscreen controls intact. Later CPU updates must preserve
     * that copied menu rather than upload the stale CPU bytes over it. */
    C2D_Prepare();
    C3D_RenderTarget *bottom=C3D_RenderTargetCreate(240,320,GPU_RB_RGB565,-1); assert(bottom);
    C2D_SceneBegin(bottom); C2D_SceneSize(240,320,true); C2D_ViewReset();
    C2D_DrawRectSolid(0,0,0,320,240,C2D_Color32(255,0,0,255)); C2D_Flush();
    gfxSetScreenFormat(GFX_BOTTOM,GSP_RGB565_OES);
    u16 *lcd=(u16 *)gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL);
    for(unsigned x=272;x<320;x++) for(unsigned y=0;y<240;y++) lcd[x*240+y]=0x001f;
    C3D_SyncDisplayTransfer(bottom->frameBuf.colorBuf,GX_BUFFER_DIM(240,272),(u32 *)lcd,GX_BUFFER_DIM(240,272),GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565));
    for(unsigned y=0;y<240;y++) lcd[300*240+y]=0x07e0;
    GSPGPU_FlushDataCache(lcd+300*240,480); gpuFlushScreens();
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_BOTTOM));
    pixel(100,100,255,0,0,255); pixel(100,280,0,0,255,255); pixel(100,300,0,255,0,255);
    /* The CPU canvas can repaint identical bytes after the GPU has replaced
     * that area. Its explicit flush, not just a byte difference, commits it. */
    memset(lcd+100*240,0,480);
    GSPGPU_FlushDataCache(lcd+100*240,480); gpuFlushScreens();
    pixel(100,100,0,0,0,255); pixel(100,101,255,0,0,255);
    C3D_SyncDisplayTransfer(bottom->frameBuf.colorBuf,GX_BUFFER_DIM(240,272),(u32 *)lcd,GX_BUFFER_DIM(240,272),GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565));
    /* Two separate flushes must not re-upload the stale CPU shadow between
     * them and erase the GPU compositor's intervening columns. */
    GSPGPU_FlushDataCache(lcd+20*240,480);
    GSPGPU_FlushDataCache(lcd+200*240,480); gpuFlushScreens();
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_BOTTOM));
    pixel(100,20,0,0,0,255); pixel(100,200,0,0,0,255);
    pixel(100,100,255,0,0,255); pixel(100,280,0,0,255,255);
    /* Both origin present paths must advance the same simulation tick group:
     * the ordinary FrameBegin/End and held-top menus' gspWaitForVBlank. */
    unsigned before=gpuTestPresentCount(),skipped=0;
    gameSpeed=4;
    for(unsigned tick=0;tick<8;tick++) {
        if(C3D_FrameBegin(0)) C3D_FrameEnd(0); else skipped++;
    }
    assert(skipped==6 && gpuTestPresentCount()==before+2);
    gameSpeed=3; before=gpuTestPresentCount();
    for(unsigned tick=0;tick<6;tick++) gspWaitForVBlank();
    assert(gpuTestPresentCount()==before+2);
    gameSpeed=4; assert(C3D_FrameBegin(0)); C3D_FrameEnd(0);
    before=gpuTestPresentCount(); gspWaitForVBlank();
    assert(gpuTestPresentCount()==before);
    gameSpeed=2; gspWaitForVBlank();
    assert(gpuTestPresentCount()==before+1);
    gpuListener(CTR_APT_SUSPEND,NULL); gpuListener(CTR_APT_RESUME,NULL);
    gspWaitForVBlank(); assert(gpuTestPresentCount()==before+2);
    gameSpeed=1;
    for(unsigned tick=0;tick<3;tick++) {
        assert(C3D_FrameBegin(0)); C3D_FrameEnd(0);
    }
    assert(gpuTestPresentCount()==before+5);
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTestScreenFramebuffer(GFX_BOTTOM));
    pixel(100,100,255,0,0,255); pixel(100,280,0,0,255,255);
    assert(state==CTR_HOST_RUNNING); assert(glGetError()==GL_NO_ERROR);
    printf("PASS %d GLES pixel assertions and fast-forward presentation scheduling: 2D, tiling, flips, tint, CPU edits, arena-backed texture views/reuse, TexEnv/cache, alpha, scissor, blending, FBO sampling, rotation, translated voxel shader, packed vertices, depth, RGBA5551 masks, suspend/resume, mixed CPU/GPU bottom display\n",checks);
    if(argc==3) {
        /* Upstream's atlas is 1024x1024, but a typed glyph can change one
         * 8x8 tile. Measure that real update pattern separately from VBlank. */
        C3D_Tex atlas={0}; assert(C3D_TexInit(&atlas,1024,1024,GPU_RGBA5551));
        Tex3DS_SubTexture glyph={8,8,0,1,8.f/1024,1-8.f/1024};
        C2D_Prepare(); C2D_SceneBegin(target); C2D_ViewReset();
        C2D_DrawImageAt((C2D_Image){&atlas,&glyph},0,0,0,NULL,1,1); C2D_Flush(); glFinish();
        double start=gpuNow();
        for(int i=0;i<60;i++) {
            for(int p=0;p<64;p++) ((u16 *)atlas.data)[p]=(i&1)?0xF801:0x07C1;
            C2D_DrawImageAt((C2D_Image){&atlas,&glyph},0,0,0,NULL,1,1); C2D_Flush(); glFinish();
        }
        printf("BENCH 1024-square atlas, one modified tile, 60 upload/draw/finish iterations: %.3f ms/update\n",(gpuNow()-start)*1000/60);
        start=gpuNow();
        for(int i=0;i<60;i++) {
            for(int sprite=0;sprite<17;sprite++) {
                C2D_ViewReset(); C2D_ViewTranslate(sprite%2,sprite%3);
                C2D_DrawImageAt((C2D_Image){&atlas,&glyph},0,0,0,NULL,1,1);
            }
            C2D_Flush(); glFinish();
        }
        printf("BENCH 17 independently transformed sprites sharing an unchanged atlas: %.3f ms/frame\n",(gpuNow()-start)*1000/60);
        /* Separate driver submission from large atlas scans and display
         * swaps. The real translated voxel shader changes one register in
         * the second case, as its lighting/model parameters do between draws. */
        C3D_BindProgram(&program);
        for(int i=0;i<6;i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
        C3D_TexEnvSrc(C3D_GetTexEnv(0),C3D_Both,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
        for(int unit=0;unit<3;unit++) C3D_TexBind(unit,NULL);
        C3D_AlphaTest(false,GPU_ALWAYS,0); C3D_ColorLogicOp(GPU_LOGICOP_COPY);
        C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,shade,2,0,0,0);
        for(int changed=0;changed<2;changed++) {
            C3D_DrawArrays(GPU_TRIANGLES,0,3); glFinish();
            uniformCalls=uniformVectors=textureBinds=attributeCalls=0;
            start=gpuNow();
            double cpuStart=callerCpuTime();
            for(int i=0;i<4000;i++) {
                if(changed) C3D_FVUnif[0][shade].x=(i&1)?2:1;
                C3D_DrawArrays(GPU_TRIANGLES,0,3);
            }
            glFinish();
            printf("BENCH voxel %s uniforms, 4000 draws: %.3f ms, %u uniform calls, %u vec4 values, %u texture binds, %u attribute layout calls, %.3f ms caller CPU\n",
                   changed?"changing":"unchanged",(gpuNow()-start)*1000,uniformCalls,uniformVectors,textureBinds,attributeCalls,(callerCpuTime()-cpuStart)*1000);
            pixel(8,8,255,0,0,255);
        }
        C3D_TexDelete(&atlas);
    }
    C2D_Fini(); C3D_Fini(); shaderProgramFree(&program); DVLB_Free(binary); gfxExit(); return 0;
}
