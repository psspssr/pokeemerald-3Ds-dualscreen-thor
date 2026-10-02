#include "gpu_internal.h"
#include <stdio.h>

/* Origin deliberately reads these two fields of Citro3D 1.7.1's context when
 * releasing texture unit 1. Keep that small documented compatibility surface. */
unsigned char __C3D_Context[0x140] __attribute__((aligned(8)));
C3D_FVec C3D_FVUnif[2][C3D_FVUNIF_COUNT];
C3D_IVec C3D_IVUnif[2][C3D_IVUNIF_COUNT];
u16 C3D_BoolUnifs[2];
bool C3D_FVUnifDirty[2][C3D_FVUNIF_COUNT],C3D_IVUnifDirty[2][C3D_IVUNIF_COUNT],C3D_BoolUnifsDirty[2];
C3D_TexEnv gpuEnvs[6];
C3D_AttrInfo gpuAttrs;
C3D_BufInfo gpuBuffers;
shaderProgram_s *gpuProgram;
bool gpuAlphaEnabled;
GPU_TESTFUNC gpuAlphaFunc;
int gpuAlphaRef;
static bool depthEnabled,blendEnabled=true;
static GPU_TESTFUNC depthFunc=GPU_ALWAYS;
static GPU_WRITEMASK writeMask=GPU_WRITE_ALL;
static GPU_CULLMODE cullMode=GPU_CULL_NONE;
static GPU_SCISSORMODE scissorMode;
static GLint scissorBox[4];
static GLfloat blendColor[4];
static GLenum blendEquations[2]={GL_FUNC_ADD,GL_FUNC_ADD};
static GLenum blendFactors[4]={GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA};
static GLuint drawVao,drawVbo[12];
static C3D_AttrInfo drawAttributes;
static C3D_BufInfo drawBufferLayout;
static bool drawLayoutValid;
static unsigned frameCount;
static double frameStart,drawingTime;
static void (*endHook)(void *);
static void *endHookParam;
static const GLenum tests[]={GL_NEVER,GL_ALWAYS,GL_EQUAL,GL_NOTEQUAL,GL_LESS,GL_LEQUAL,GL_GREATER,GL_GEQUAL};

bool C3D_Init(size_t size)
{
    (void)size;
    if(!gpuInit()) return false;
    for(int i=0;i<6;i++) C3D_TexEnvInit(&gpuEnvs[i]);
    if(!drawVao) { glGenVertexArrays(1,&drawVao); glGenBuffers(12,drawVbo); drawLayoutValid=false; }
    gpuApplyState(); return true;
}
void C3D_Fini(void)
{
    gpuC2DFlush();
    while(gpuTargets) C3D_RenderTargetDelete(gpuTargets->target);
    while(gpuTextures) C3D_TexDelete(gpuTextures->tex);
    glDeleteVertexArrays(1,&drawVao); glDeleteBuffers(12,drawVbo); drawVao=0; drawLayoutValid=false;
}
void gpuApplyState(void)
{
    if(depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthFunc(tests[depthFunc&7]); glDepthMask((writeMask&GPU_WRITE_DEPTH)?GL_TRUE:GL_FALSE);
    glColorMask((writeMask&1)!=0,(writeMask&2)!=0,(writeMask&4)!=0,(writeMask&8)!=0);
    if(blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendEquationSeparate(blendEquations[0],blendEquations[1]);
    glBlendFuncSeparate(blendFactors[0],blendFactors[1],blendFactors[2],blendFactors[3]);
    glBlendColor(blendColor[0],blendColor[1],blendColor[2],blendColor[3]);
    if(cullMode==GPU_CULL_NONE) glDisable(GL_CULL_FACE); else { glEnable(GL_CULL_FACE); glFrontFace(GL_CCW); glCullFace(cullMode==GPU_CULL_FRONT_CCW?GL_FRONT:GL_BACK); }
    if(scissorMode==GPU_SCISSOR_NORMAL) { glEnable(GL_SCISSOR_TEST); glScissor(scissorBox[0],scissorBox[1],scissorBox[2],scissorBox[3]); } else glDisable(GL_SCISSOR_TEST);
}
void C3D_CullFace(GPU_CULLMODE mode) { gpuC2DFlush(); cullMode=mode; gpuApplyState(); }
void C3D_DepthTest(bool enable,GPU_TESTFUNC function,GPU_WRITEMASK mask)
{ gpuC2DFlush(); depthEnabled=enable; depthFunc=function; writeMask=mask; gpuApplyState(); }
void C3D_AlphaTest(bool enable,GPU_TESTFUNC function,int ref)
{ gpuC2DFlush(); gpuAlphaEnabled=enable; gpuAlphaFunc=function; gpuAlphaRef=ref&255; }
void C3D_AlphaBlend(GPU_BLENDEQUATION color,GPU_BLENDEQUATION alpha,GPU_BLENDFACTOR sr,GPU_BLENDFACTOR dr,GPU_BLENDFACTOR sa,GPU_BLENDFACTOR da)
{
    static const GLenum equations[]={GL_FUNC_ADD,GL_FUNC_SUBTRACT,GL_FUNC_REVERSE_SUBTRACT,GL_MIN,GL_MAX};
    static const GLenum factors[]={GL_ZERO,GL_ONE,GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR,GL_DST_COLOR,GL_ONE_MINUS_DST_COLOR,
        GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_DST_ALPHA,GL_ONE_MINUS_DST_ALPHA,GL_CONSTANT_COLOR,GL_ONE_MINUS_CONSTANT_COLOR,
        GL_CONSTANT_ALPHA,GL_ONE_MINUS_CONSTANT_ALPHA,GL_SRC_ALPHA_SATURATE};
    if((unsigned)color>=5 || (unsigned)alpha>=5 || (unsigned)sr>=15 || (unsigned)dr>=15 || (unsigned)sa>=15 || (unsigned)da>=15) return;
    gpuC2DFlush(); blendEnabled=true; blendEquations[0]=equations[color]; blendEquations[1]=equations[alpha];
    blendFactors[0]=factors[sr]; blendFactors[1]=factors[dr]; blendFactors[2]=factors[sa]; blendFactors[3]=factors[da]; gpuApplyState();
}
void C3D_BlendingColor(u32 color)
{ gpuC2DFlush(); for(int i=0;i<4;i++) blendColor[i]=((color>>(8*i))&255)/255.f; gpuApplyState(); }
void C3D_ColorLogicOp(GPU_LOGICOP operation)
{
    gpuC2DFlush();
    if(operation!=GPU_LOGICOP_COPY) { GPU_LOG("unsupported logic operation %u",operation); CtrHost_SetState(CTR_HOST_EXITING); }
    blendEnabled=false; gpuApplyState();
}
void C3D_SetScissor(GPU_SCISSORMODE mode,u32 left,u32 bottom,u32 right,u32 top)
{
    gpuC2DFlush(); scissorMode=mode;
    scissorBox[0]=left; scissorBox[1]=bottom; scissorBox[2]=right>left?right-left:0; scissorBox[3]=top>bottom?top-bottom:0;
    if(mode==GPU_SCISSOR_INVERT) { GPU_LOG("inverted scissor unsupported"); CtrHost_SetState(CTR_HOST_EXITING); }
    gpuApplyState();
}
void C3D_SetViewport(u32 x,u32 y,u32 width,u32 height) { gpuC2DFlush(); glViewport(x,y,width,height); }
void C3D_DepthMap(bool isZ,float scale,float offset) { (void)isZ; glDepthRangef(offset+scale,offset); }
void C3D_StencilTest(bool enable,GPU_TESTFUNC function,int ref,int inputMask,int mask)
{ gpuC2DFlush(); if(enable) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST); glStencilFunc(tests[function&7],ref,inputMask); glStencilMask(mask); }
void C3D_StencilOp(GPU_STENCILOP fail,GPU_STENCILOP zfail,GPU_STENCILOP pass)
{ static const GLenum ops[]={GL_KEEP,GL_ZERO,GL_REPLACE,GL_INCR,GL_DECR,GL_INVERT,GL_INCR_WRAP,GL_DECR_WRAP}; gpuC2DFlush(); glStencilOp(ops[fail&7],ops[zfail&7],ops[pass&7]); }

C3D_TexEnv *C3D_GetTexEnv(int index) { gpuC2DFlush(); return (unsigned)index<6?&gpuEnvs[index]:NULL; }
void C3D_SetTexEnv(int index,C3D_TexEnv *env) { gpuC2DFlush(); if((unsigned)index<6 && env) gpuEnvs[index]=*env; }
void C3D_DirtyTexEnv(C3D_TexEnv *env) { (void)env; gpuC2DFlush(); }
void C3D_BindProgram(shaderProgram_s *program) { gpuC2DFlush(); gpuProgram=program; }
void C3D_UpdateUniforms(GPU_SHADER_TYPE type) { (void)type; /* Uploaded at each draw, including direct write pointers. */ }
void AttrInfo_Init(C3D_AttrInfo *info) { memset(info,0,sizeof(*info)); }
int AttrInfo_AddLoader(C3D_AttrInfo *info,int reg,GPU_FORMATS format,int count)
{
    if(!info || info->attrCount>=12 || reg<0 || reg>=16 || count<1 || count>4) return -1;
    int index=info->attrCount++;
    info->flags[index/8]|=(u32)(format|((count-1)<<2))<<((index%8)*4);
    info->permutation|=(u64)reg<<(index*4); return index;
}
int AttrInfo_AddFixed(C3D_AttrInfo *info,int reg) { (void)info; (void)reg; return -1; }
C3D_AttrInfo *C3D_GetAttrInfo(void) { gpuC2DFlush(); return &gpuAttrs; }
void C3D_SetAttrInfo(C3D_AttrInfo *info) { gpuC2DFlush(); gpuAttrs=*info; }
void BufInfo_Init(C3D_BufInfo *info) { memset(info,0,sizeof(*info)); }
int BufInfo_Add(C3D_BufInfo *info,const void *data,int stride,int count,u64 permutation)
{
    if(!info || info->bufCount>=12 || stride<=0 || count<1 || count>12) return -1;
    int index=info->bufCount++; info->buffers[index]=(C3D_BufCfg){data,stride,count,permutation}; return index;
}
C3D_BufInfo *C3D_GetBufInfo(void) { gpuC2DFlush(); return &gpuBuffers; }
void C3D_SetBufInfo(C3D_BufInfo *info) { gpuC2DFlush(); gpuBuffers=*info; }
static bool vertexLayoutMatches(void)
{
    if(!drawLayoutValid || drawAttributes.attrCount!=gpuAttrs.attrCount ||
       drawAttributes.flags[0]!=gpuAttrs.flags[0] || drawAttributes.flags[1]!=gpuAttrs.flags[1] ||
       drawAttributes.permutation!=gpuAttrs.permutation ||
       drawBufferLayout.bufCount!=gpuBuffers.bufCount) return false;
    for(int b=0;b<gpuBuffers.bufCount;b++) {
        const C3D_BufCfg *old=&drawBufferLayout.buffers[b],*now=&gpuBuffers.buffers[b];
        if(old->stride!=now->stride || old->count!=now->count || old->permutation!=now->permutation) return false;
    }
    return true;
}
static bool configureVertexLayout(void)
{
    drawLayoutValid=false;
    for(int i=0;i<16;i++) { glDisableVertexAttribArray(i); glVertexAttrib4f(i,0,0,0,1); }
    static const GLenum formats[]={GL_BYTE,GL_UNSIGNED_BYTE,GL_SHORT,GL_FLOAT};
    static const unsigned sizes[]={1,1,2,4};
    for(int b=0;b<gpuBuffers.bufCount;b++) {
        const C3D_BufCfg *buffer=&gpuBuffers.buffers[b];
        if(buffer->stride<=0 || buffer->count<1 || buffer->count>12) return false;
        glBindBuffer(GL_ARRAY_BUFFER,drawVbo[b]);
        size_t offset=0;
        for(int a=0;a<buffer->count;a++) {
            unsigned index=(buffer->permutation>>(a*4))&15;
            if(index>=(unsigned)gpuAttrs.attrCount) return false;
            unsigned format=(gpuAttrs.flags[index/8]>>((index%8)*4))&15;
            unsigned reg=(gpuAttrs.permutation>>(index*4))&15;
            unsigned components=(format>>2)+1,type=format&3;
            if(offset+components*sizes[type]>(unsigned)buffer->stride) return false;
            glEnableVertexAttribArray(reg); glVertexAttribPointer(reg,components,formats[type],GL_FALSE,buffer->stride,(const void *)offset);
            offset+=components*sizes[type];
        }
    }
    drawAttributes=gpuAttrs; drawBufferLayout=gpuBuffers; drawLayoutValid=true;
    return true;
}
void C3D_DrawArrays(GPU_Primitive_t primitive,int first,int count)
{
    if(first<0 || count<=0 || !gpuTarget || !gpuProgram || !gpuProgram->vertexShader) return;
    if((unsigned)gpuBuffers.bufCount>12 || (unsigned)gpuAttrs.attrCount>12) return;
    for(int b=0;b<gpuBuffers.bufCount;b++) if(!gpuBuffers.buffers[b].data) return;
    gpuC2DFlush();
    if(!gpuUseProgram(false)) { CtrHost_SetState(CTR_HOST_EXITING); return; }
    glBindVertexArray(drawVao);
    /* The VAO retains bindings to these fixed VBO names. A different CPU
     * pointer, first/count or new vertex bytes only replaces their storage;
     * it does not change the layout. 2D/presentation use separate VAOs. */
    if(!vertexLayoutMatches() && !configureVertexLayout()) return;
    for(int b=0;b<gpuBuffers.bufCount;b++) {
        C3D_BufCfg *buffer=&gpuBuffers.buffers[b];
        glBindBuffer(GL_ARRAY_BUFFER,drawVbo[b]);
        glBufferData(GL_ARRAY_BUFFER,(size_t)count*buffer->stride,(const char *)buffer->data+(size_t)first*buffer->stride,GL_STREAM_DRAW);
    }
    gpuApplyState();
    GLenum mode=primitive==GPU_TRIANGLE_STRIP?GL_TRIANGLE_STRIP:primitive==GPU_TRIANGLE_FAN?GL_TRIANGLE_FAN:GL_TRIANGLES;
    glDrawArrays(mode,0,count);
}

bool C3D_FrameBegin(u8 flags)
{
    (void)flags;
    if(!gpuInit() || CtrHost_GetState()==CTR_HOST_EXITING) return false;
    /* Logic, input, audio synthesis and VBlank have already advanced. Only
     * skip the picture; the next submitted frame keeps the 59.83 Hz clock. */
    if(!gpuShouldRender()) return false;
    frameStart=gpuNow();
    for(GpuTarget *t=gpuTargets;t;t=t->next) t->target->used=false;
    gpuC2DResetFrame(); return true;
}
bool C3D_FrameDrawOn(C3D_RenderTarget *target)
{ if(!target) return false; C3D_SetFrameBuf(&target->frameBuf); target->used=true; return gpuTarget!=NULL; }
void C3D_FrameSplit(u8 flags) { (void)flags; gpuC2DFlush(); glFlush(); }
void C3D_FrameEnd(u8 flags)
{
    (void)flags; gpuC2DFlush();
    for(GpuTarget *t=gpuTargets;t;t=t->next)
        if(t->target->linked && t->target->used && t->target->side==GFX_LEFT)
            gpuTransferToScreen(t,t->target->screen,t->target->frameBuf.width,t->target->frameBuf.height);
    drawingTime=(gpuNow()-frameStart)*1000; gpuPresent(); frameCount++;
    if(endHook) endHook(endHookParam);
    gpuPace();
}
void C3D_FrameSync(void) { gpuC2DFlush(); glFinish(); }
u32 C3D_FrameCounter(int id) { (void)id; return frameCount; }
void C3D_FrameEndHook(void (*hook)(void *),void *param) { endHook=hook; endHookParam=param; }
float C3D_GetDrawingTime(void) { return drawingTime; }
float C3D_GetProcessingTime(void) { return drawingTime; }

void C3D_SyncTextureCopy(u32 *input,u32 inputDim,u32 *output,u32 outputDim,u32 size,u32 flags)
{
    (void)flags; gpuC2DFlush();
    if(!input || !output) return;
    /* TextureCopy dimensions describe 16-byte runs and gaps, not pixel dimensions. */
    unsigned inWidth=(inputDim&0xffff)*16,inGap=(inputDim>>16)*16;
    unsigned outWidth=(outputDim&0xffff)*16,outGap=(outputDim>>16)*16;
    if(!inWidth && !outWidth) memmove(output,input,size);
    else {
        const unsigned char *src=(const unsigned char *)input; unsigned char *dst=(unsigned char *)output;
        unsigned inLeft=inWidth?inWidth:size,outLeft=outWidth?outWidth:size;
        while(size) {
            unsigned run=size; if(run>inLeft) run=inLeft; if(run>outLeft) run=outLeft;
            memmove(dst,src,run); src+=run; dst+=run; size-=run; inLeft-=run; outLeft-=run;
            if(!inLeft) { src+=inGap; inLeft=inWidth?inWidth:size; }
            if(!outLeft) { dst+=outGap; outLeft=outWidth?outWidth:size; }
        }
    }
    for(GpuTexture *t=gpuTextures;t;t=t->next)
        if((uintptr_t)output>=(uintptr_t)t->tex->data && (uintptr_t)output<(uintptr_t)t->tex->data+t->tex->size) t->authoritative=false;
}
void C3D_SyncDisplayTransfer(u32 *input,u32 inDim,u32 *output,u32 outDim,u32 flags)
{
    gpuC2DFlush();
    for(GpuTarget *t=gpuTargets;t;t=t->next) if(t->target->frameBuf.colorBuf==input) {
        for(int i=0;i<2;i++) if((void *)gfxGetFramebuffer((gfxScreen_t)i,GFX_LEFT,NULL,NULL)==output) {
            gpuTransferToScreen(t,(gfxScreen_t)i,outDim&65535,outDim>>16); return;
        }
    }
    if(flags&GX_TRANSFER_RAW_COPY(1)) { memmove(output,input,(size_t)(inDim&65535)*(inDim>>16)*4); return; }
    GPU_LOG("unhandled display transfer source/destination (flags 0x%x)",flags); CtrHost_SetState(CTR_HOST_EXITING);
}
void C3D_SyncMemoryFill(u32 *a,u32 av,u32 *ae,u16 ac,u32 *b,u32 bv,u32 *be,u16 bc)
{
    gpuC2DFlush(); u32 *starts[]={a,b},*ends[]={ae,be},values[]={av,bv}; u16 controls[]={ac,bc};
    for(int i=0;i<2;i++) {
        if(!starts[i] || !ends[i] || ends[i]<starts[i]) continue;
        unsigned bytes=(controls[i]&0x200)?4:(controls[i]&0x100)?3:2;
        unsigned char *out=(unsigned char *)starts[i],*end=(unsigned char *)ends[i];
        for(;out+bytes<=end;out+=bytes) memcpy(out,&values[i],bytes);
    }
}
Result GX_TextureCopy(u32 *a,u32 b,u32 *c,u32 d,u32 e,u32 f) { C3D_SyncTextureCopy(a,b,c,d,e,f); return 0; }
Result GX_DisplayTransfer(u32 *a,u32 b,u32 *c,u32 d,u32 e) { C3D_SyncDisplayTransfer(a,b,c,d,e); return 0; }
Result GX_MemoryFill(u32 *a,u32 b,u32 *c,u16 d,u32 *e,u32 f,u32 *g,u16 h) { C3D_SyncMemoryFill(a,b,c,d,e,f,g,h); return 0; }
