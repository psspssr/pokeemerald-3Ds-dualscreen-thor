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

static bool validTextureView(const C3D_Tex *texture)
{
    return texture && texture->data && texture->width>=8 && texture->height>=8 &&
        texture->width<=2048 && texture->height<=2048 &&
        !(texture->width&(texture->width-1)) && !(texture->height&(texture->height-1)) &&
        texture->fmt<GPU_ETC1 && ((texture->param>>28)&7)==GPU_TEX_2D &&
        texture->size==gpuTextureSize(texture->width,texture->height,texture->fmt);
}

static bool configureRecord(GpuTexture *record,C3D_Tex *texture)
{
    unsigned char *shadow=calloc(1,texture->size);
    unsigned char *rgba=calloc((size_t)texture->width*texture->height,4);
    if(!shadow || !rgba) { free(shadow); free(rgba); return false; }
    free(record->shadow); free(record->rgba);
    record->shadow=shadow; record->rgba=rgba; record->tex=texture;
    record->data=texture->data; record->width=texture->width; record->height=texture->height;
    record->size=texture->size; record->format=texture->fmt;
    record->authoritative=false; record->uploaded=false;
    if(!record->id) glGenTextures(1,&record->id);
    glBindTexture(GL_TEXTURE_2D,record->id);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,texture->width,texture->height,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    gpuSetTextureParams(texture);
    return true;
}

static GpuTexture *ensureTextureRecord(C3D_Tex *texture)
{
    if(!validTextureView(texture)) { GPU_LOG("invalid or unsupported texture view"); return NULL; }
    GpuTexture *record=gpuFindTexture(texture);
    if(!record) {
        /* Origin's building pages construct C3D_Tex directly over arena slices.
         * This record owns GPU resources and shadows, never the caller's data. */
        record=calloc(1,sizeof(*record));
        if(!record) return NULL;
        if(!configureRecord(record,texture)) { free(record); return NULL; }
        record->next=gpuTextures; gpuTextures=record;
    } else if(record->data!=texture->data || record->width!=texture->width ||
              record->height!=texture->height || record->size!=texture->size || record->format!=texture->fmt) {
        /* PageFree clears the descriptor; PageTexInit can reuse that same slot
         * with a different arena slice or size without calling TexDelete. */
        if(record->ownsData) { GPU_LOG("owned texture allocation changed without deletion"); return NULL; }
        if(!configureRecord(record,texture)) return NULL;
    }
    return record;
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
    texture->width=p.width; texture->height=p.height; texture->fmt=p.format;
    texture->size=size; texture->maxLevel=p.maxLevel;
    if(!texture->data || !configureRecord(record,texture)) {
        if(p.onVram) vramFree(texture->data); else linearFree(texture->data);
        free(record->shadow); free(record->rgba); free(record); memset(texture,0,sizeof(*texture)); return false;
    }
    memset(texture->data,0,total); record->ownsData=true;
    CtrMem_SetOwner(texture->data,record);
    record->next=gpuTextures; gpuTextures=record;
    return true;
}

GLuint gpuTextureId(C3D_Tex *texture)
{
    GpuTexture *record=ensureTextureRecord(texture);
    if(!record) return 0;
    glBindTexture(GL_TEXTURE_2D,record->id); gpuSetTextureParams(texture);
    if(!record->authoritative && (!record->uploaded || memcmp(record->shadow,texture->data,texture->size))) {
        /* Origin edits atlas tiles directly, without a TexFlush per glyph.
         * PICA stores an eight-pixel-high tile row contiguously. Compare those
         * rows, then combine adjacent changed rows into one upload. Updating
         * one glyph must not detile/upload the entire 1024-square atlas. */
        unsigned rows=texture->height/8;
        size_t rowBytes=texture->size/rows;
        int first=-1;
        for(unsigned row=0;row<=rows;row++) {
            bool changed=row<rows && (!record->uploaded ||
                memcmp(record->shadow+row*rowBytes,(unsigned char *)texture->data+row*rowBytes,rowBytes));
            if(changed && first<0) first=(int)row;
            if(!changed && first>=0) {
                unsigned height=(row-(unsigned)first)*8,glY=(rows-row)*8;
                const unsigned char *source=(unsigned char *)texture->data+(unsigned)first*rowBytes;
                unsigned char *pixels=record->rgba+(size_t)glY*texture->width*4;
                if(!gpuDecodeTexture(source,pixels,texture->width,height,texture->fmt)) {
                    GPU_LOG("unsupported texture format %u",texture->fmt); return 0;
                }
                glTexSubImage2D(GL_TEXTURE_2D,0,0,glY,texture->width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
                memcpy(record->shadow+(unsigned)first*rowBytes,source,(row-(unsigned)first)*rowBytes);
                first=-1;
            }
        }
        record->uploaded=true;
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
    if(t->ownsData) {
        CtrMemBlock block;
        if(CtrMem_Find(t->data,&block) && block.kind==CTR_MEM_VRAM) vramFree(t->data); else linearFree(t->data);
    }
    free(t); memset(texture,0,sizeof(*texture));
}

u32 C3D_CalcColorBufSize(u32 w,u32 h,GPU_COLORBUF format) { return gpuTextureSize(w,h,(GPU_TEXCOLOR)format); }
u32 C3D_CalcDepthBufSize(u32 w,u32 h,GPU_DEPTHBUF format) { return w*h*(format==GPU_RB_DEPTH16?2:4); }

static void targetStorage(unsigned width,unsigned height,GPU_COLORBUF format)
{
    /* Preserve the quantization and one-bit alpha of the 3DS render buffers.
     * In particular, the composited RGBA5551 depth planes use alpha as a mask. */
    GLenum internal=GL_RGBA8,base=GL_RGBA,type=GL_UNSIGNED_BYTE;
    switch(format) {
    case GPU_RB_RGBA5551: internal=GL_RGB5_A1; type=GL_UNSIGNED_SHORT_5_5_5_1; break;
    case GPU_RB_RGBA4: internal=GL_RGBA4; type=GL_UNSIGNED_SHORT_4_4_4_4; break;
    case GPU_RB_RGB565: internal=GL_RGB565; base=GL_RGB; type=GL_UNSIGNED_SHORT_5_6_5; break;
    case GPU_RB_RGB8: internal=GL_RGB8; base=GL_RGB; break;
    default: break;
    }
    glTexImage2D(GL_TEXTURE_2D,0,internal,width,height,0,base,type,NULL);
}

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
    if(texture) {
        record->color=texture->id; texture->authoritative=true;
        glBindTexture(GL_TEXTURE_2D,record->color);
        targetStorage(width,height,color);
    }
    else {
        glGenTextures(1,&record->color); glBindTexture(GL_TEXTURE_2D,record->color);
        targetStorage(width,height,color);
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
    gpuC2DFlush(); GpuTexture *t=ensureTextureRecord(texture); if(!t || level) return NULL;
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
