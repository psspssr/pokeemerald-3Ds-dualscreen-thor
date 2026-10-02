#include "gpu_internal.h"

GpuTexture *gpuTextures;
GpuTarget *gpuTargets,*gpuTarget;

GpuTexture *gpuFindTexture(C3D_Tex *texture)
{ for(GpuTexture *t=gpuTextures;t;t=t->next) if(t->tex==texture) return t; return NULL; }
GpuTarget *gpuFindTarget(C3D_FrameBuf *buffer)
{ for(GpuTarget *t=gpuTargets;t;t=t->next) if(&t->target->frameBuf==buffer || t->target->frameBuf.colorBuf==buffer->colorBuf) return t; return NULL; }

void gpuSetTextureParams(C3D_Tex *texture)
{
    static const GLint wraps[]={GL_CLAMP_TO_EDGE,GL_CLAMP_TO_EDGE,GL_REPEAT,GL_MIRRORED_REPEAT};
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,(texture->param&2)?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,(texture->param&4)?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,wraps[(texture->param>>12)&3]);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,wraps[(texture->param>>8)&3]);
}

bool C3D_TexInitWithParams(C3D_Tex *texture,C3D_TexCube *cube,C3D_TexInitParams p)
{
    if(!texture || cube || p.type!=GPU_TEX_2D || p.width<8 || p.height<8 ||
       (p.width&(p.width-1)) || (p.height&(p.height-1)) || p.width>2048 || p.height>2048 || p.format>=GPU_ETC1 || !gpuInit()) return false;
    size_t size=gpuTextureSize(p.width,p.height,p.format),total=C3D_TexCalcTotalSize(size,p.maxLevel);
    GpuTexture *record=calloc(1,sizeof(*record));
    if(!record) return false;
    memset(texture,0,sizeof(*texture));
    texture->data=p.onVram?vramAlloc(total):linearAlloc(total);
    record->shadow=malloc(total); record->rgba=calloc((size_t)p.width*p.height,4);
    if(!texture->data || !record->shadow || !record->rgba) {
        if(p.onVram) vramFree(texture->data); else linearFree(texture->data);
        free(record->shadow); free(record->rgba); free(record); memset(texture,0,sizeof(*texture)); return false;
    }
    memset(texture->data,0,total); memset(record->shadow,0,total);
    texture->width=p.width; texture->height=p.height; texture->fmt=p.format;
    texture->size=size; texture->maxLevel=p.maxLevel; record->tex=texture;
    glGenTextures(1,&record->id); glBindTexture(GL_TEXTURE_2D,record->id);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,p.width,p.height,0,GL_RGBA,GL_UNSIGNED_BYTE,record->rgba);
    gpuSetTextureParams(texture);
    CtrMem_SetOwner(texture->data,record);
    record->next=gpuTextures; gpuTextures=record;
    return true;
}

GLuint gpuTextureId(C3D_Tex *texture)
{
    GpuTexture *record=gpuFindTexture(texture);
    if(!record) return 0;
    glBindTexture(GL_TEXTURE_2D,record->id); gpuSetTextureParams(texture);
    if(!record->authoritative && (!record->uploaded || memcmp(record->shadow,texture->data,texture->size))) {
        if(!gpuDecodeTexture(texture->data,record->rgba,texture->width,texture->height,texture->fmt)) {
            GPU_LOG("unsupported texture format %u",texture->fmt); return 0;
        }
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,texture->width,texture->height,GL_RGBA,GL_UNSIGNED_BYTE,record->rgba);
        memcpy(record->shadow,texture->data,texture->size); record->uploaded=true;
    }
    return record->id;
}
void C3D_TexFlush(C3D_Tex *texture)
{ gpuC2DFlush(); GpuTexture *t=gpuFindTexture(texture); if(t) { t->authoritative=false; t->uploaded=false; } }
void C3D_TexLoadImage(C3D_Tex *texture,const void *data,GPU_TEXFACE face,int level)
{
    (void)face;
    if(!texture || !data || level<0 || level>texture->maxLevel) return;
    gpuC2DFlush(); u32 size; void *out=C3D_Tex2DGetImagePtr(texture,level,&size); memcpy(out,data,size); C3D_TexFlush(texture);
}
void C3D_TexGenerateMipmap(C3D_Tex *texture,GPU_TEXFACE face)
{ (void)face; gpuC2DFlush(); if(gpuTextureId(texture)) glGenerateMipmap(GL_TEXTURE_2D); }
void C3D_TexBind(int unit,C3D_Tex *texture)
{ if(unit<0 || unit>2) return; gpuC2DFlush(); GPU_BOUND_TEXTURES[unit]=texture; }
void C3D_TexDelete(C3D_Tex *texture)
{
    gpuC2DFlush();
    GpuTexture **item=&gpuTextures;
    while(*item && (*item)->tex!=texture) item=&(*item)->next;
    if(!*item) return;
    GpuTexture *t=*item; *item=t->next;
    for(int i=0;i<3;i++) if(GPU_BOUND_TEXTURES[i]==texture) GPU_BOUND_TEXTURES[i]=NULL;
    glDeleteTextures(1,&t->id); free(t->shadow); free(t->rgba);
    CtrMemBlock block;
    if(CtrMem_Find(texture->data,&block) && block.kind==CTR_MEM_VRAM) vramFree(texture->data); else linearFree(texture->data);
    free(t); memset(texture,0,sizeof(*texture));
}

u32 C3D_CalcColorBufSize(u32 w,u32 h,GPU_COLORBUF format) { return gpuTextureSize(w,h,(GPU_TEXCOLOR)format); }
u32 C3D_CalcDepthBufSize(u32 w,u32 h,GPU_DEPTHBUF format) { return w*h*(format==GPU_RB_DEPTH16?2:4); }

static C3D_RenderTarget *createTarget(int width,int height,GPU_COLORBUF color,C3D_DEPTHTYPE depth,GpuTexture *texture)
{
    if(width<=0 || height<=0 || width>4096 || height>4096 || !gpuInit()) return NULL;
    C3D_RenderTarget *target=calloc(1,sizeof(*target));
    GpuTarget *record=calloc(1,sizeof(*record));
    if(!target || !record) { free(target); free(record); return NULL; }
    target->frameBuf.width=width; target->frameBuf.height=height; target->frameBuf.colorFmt=color;
    target->frameBuf.colorMask=15; target->frameBuf.depthFmt=depth.__e;
    target->ownsColor=!texture; target->ownsDepth=depth.__i>=0;
    target->frameBuf.colorBuf=texture?texture->tex->data:vramAlloc(C3D_CalcColorBufSize(width,height,color));
    if(target->ownsDepth) target->frameBuf.depthBuf=vramAlloc(C3D_CalcDepthBufSize(width,height,depth.__e));
    if(!target->frameBuf.colorBuf || (target->ownsDepth && !target->frameBuf.depthBuf)) {
        if(target->ownsColor) vramFree(target->frameBuf.colorBuf);
        vramFree(target->frameBuf.depthBuf); free(target); free(record); return NULL;
    }
    record->target=target; record->texture=texture;
    if(texture) { record->color=texture->id; texture->authoritative=true; }
    else {
        glGenTextures(1,&record->color); glBindTexture(GL_TEXTURE_2D,record->color);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    glGenFramebuffers(1,&record->fbo); glBindFramebuffer(GL_FRAMEBUFFER,record->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,record->color,0);
    if(target->ownsDepth) {
        glGenRenderbuffers(1,&record->depth); glBindRenderbuffer(GL_RENDERBUFFER,record->depth);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,record->depth);
        target->frameBuf.depthMask=2;
    }
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
        GPU_LOG("incomplete framebuffer %dx%d",width,height);
        glDeleteFramebuffers(1,&record->fbo); glDeleteRenderbuffers(1,&record->depth);
        if(!texture) { glDeleteTextures(1,&record->color); vramFree(target->frameBuf.colorBuf); }
        vramFree(target->frameBuf.depthBuf); free(target); free(record); return NULL;
    }
    record->next=gpuTargets; gpuTargets=record;
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTarget?gpuTarget->fbo:0);
    return target;
}
C3D_RenderTarget *C3D_RenderTargetCreate(int width,int height,GPU_COLORBUF color,C3D_DEPTHTYPE depth)
{ gpuC2DFlush(); return createTarget(width,height,color,depth,NULL); }
C3D_RenderTarget *C3D_RenderTargetCreateFromTex(C3D_Tex *texture,GPU_TEXFACE face,int level,C3D_DEPTHTYPE depth)
{
    (void)face;
    gpuC2DFlush(); GpuTexture *t=gpuFindTexture(texture); if(!t || level) return NULL;
    return createTarget(texture->width,texture->height,(GPU_COLORBUF)texture->fmt,depth,t);
}
void C3D_RenderTargetDelete(C3D_RenderTarget *target)
{
    gpuC2DFlush(); GpuTarget **item=&gpuTargets;
    while(*item && (*item)->target!=target) item=&(*item)->next;
    if(!*item) return;
    GpuTarget *t=*item; *item=t->next;
    if(gpuTarget==t) { gpuTarget=NULL; glBindFramebuffer(GL_FRAMEBUFFER,0); }
    glDeleteFramebuffers(1,&t->fbo); glDeleteRenderbuffers(1,&t->depth);
    if(target->ownsColor) { glDeleteTextures(1,&t->color); vramFree(target->frameBuf.colorBuf); }
    if(target->ownsDepth) vramFree(target->frameBuf.depthBuf);
    free(target); free(t);
}
void C3D_RenderTargetSetOutput(C3D_RenderTarget *target,gfxScreen_t screen,gfx3dSide_t side,u32 flags)
{
    for(GpuTarget *t=gpuTargets;t;t=t->next)
        if(t->target->linked && t->target->screen==screen && t->target->side==side) t->target->linked=false;
    if(target) { target->linked=true; target->screen=screen; target->side=side; target->transferFlags=flags; }
}
C3D_FrameBuf *C3D_GetFrameBuf(void) { return gpuTarget?&gpuTarget->target->frameBuf:NULL; }
void C3D_SetFrameBuf(C3D_FrameBuf *buffer)
{
    gpuC2DFlush(); gpuTarget=buffer?gpuFindTarget(buffer):NULL;
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTarget?gpuTarget->fbo:0);
    if(gpuTarget) glViewport(0,0,buffer->width,buffer->height);
}
void C3D_FrameBufTex(C3D_FrameBuf *buffer,C3D_Tex *texture,GPU_TEXFACE face,int level)
{
    (void)face; (void)level;
    buffer->colorBuf=texture->data; buffer->width=texture->width; buffer->height=texture->height;
    buffer->colorFmt=(GPU_COLORBUF)texture->fmt; buffer->colorMask=15;
}
void C3D_FrameBufClear(C3D_FrameBuf *buffer,C3D_ClearBits bits,u32 color,u32 depth)
{
    gpuC2DFlush(); GpuTarget *t=gpuFindTarget(buffer); if(!t) return;
    unsigned char rgba[4]; gpuDecodePixel((unsigned char *)&color,(GPU_TEXCOLOR)buffer->colorFmt,rgba);
    glBindFramebuffer(GL_FRAMEBUFFER,t->fbo); glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_TRUE);
    glClearColor(rgba[0]/255.f,rgba[1]/255.f,rgba[2]/255.f,rgba[3]/255.f);
    glClearDepthf(buffer->depthFmt==GPU_RB_DEPTH16?(depth&65535)/65535.f:(depth&0xffffff)/16777215.f);
    glClear(((bits&C3D_CLEAR_COLOR)?GL_COLOR_BUFFER_BIT:0)|((bits&C3D_CLEAR_DEPTH)?GL_DEPTH_BUFFER_BIT:0));
    glBindFramebuffer(GL_FRAMEBUFFER,gpuTarget?gpuTarget->fbo:0); gpuApplyState();
}
void C3D_FrameBufTransfer(C3D_FrameBuf *buffer,gfxScreen_t screen,gfx3dSide_t side,u32 flags)
{ (void)flags; if(side==GFX_RIGHT) return; gpuC2DFlush(); GpuTarget *t=gpuFindTarget(buffer); if(t) gpuTransferToScreen(t,screen,buffer->width,buffer->height); }
