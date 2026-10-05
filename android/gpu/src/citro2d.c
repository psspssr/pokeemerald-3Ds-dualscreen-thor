#include "gpu_internal.h"
#include <math.h>

typedef struct { float x,y,z,u,v,r,g,b,a,blend; } Vertex2D;
static Vertex2D *vertices;
static size_t capacity,used;
static C3D_Tex *batchTexture;
static C3D_Mtx view;
static float sceneWidth,sceneHeight;
static bool sceneTilt;
static GLuint vao,vbo;

bool C2D_Init(size_t maxObjects)
{
    if(!maxObjects || maxObjects>1024*1024 || !gpuInit()) return false;
    capacity=maxObjects>4096?4096*6:maxObjects*6;
    vertices=malloc(capacity*sizeof(*vertices));
    if(!vertices) return false;
    glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo); C2D_ViewReset(); return true;
}
void C2D_Fini(void)
{ gpuC2DFlush(); free(vertices); vertices=NULL; capacity=0; glDeleteVertexArrays(1,&vao); glDeleteBuffers(1,&vbo); vao=vbo=0; }
void gpuC2DResetFrame(void) { gpuC2DFlush(); }
void gpuC2DFlush(void)
{
    if(!used) return;
    GPU_BOUND_TEXTURES[0]=batchTexture;
    if(!gpuUseProgram(GPU_PROGRAM_C2D)) { used=0; CtrHost_SetState(CTR_HOST_EXITING); return; }
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,used*sizeof(*vertices),vertices,GL_STREAM_DRAW);
    for(int i=0;i<4;i++) glEnableVertexAttribArray(i);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex2D),(void *)offsetof(Vertex2D,x));
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex2D),(void *)offsetof(Vertex2D,u));
    glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(Vertex2D),(void *)offsetof(Vertex2D,r));
    glVertexAttribPointer(3,1,GL_FLOAT,GL_FALSE,sizeof(Vertex2D),(void *)offsetof(Vertex2D,blend));
    gpuApplyState();
    if(gpuTarget && gpuTarget->texture) gpuTarget->texture->authoritative=true;
    glDrawArrays(GL_TRIANGLES,0,used); used=0;
}
void C2D_Flush(void) { gpuC2DFlush(); }
void C2D_Prepare(void)
{
    gpuC2DFlush();
    gpuC2DProgram=true;
    for(int i=0;i<6;i++) C3D_TexEnvInit(&gpuEnvs[i]);
    C3D_CullFace(GPU_CULL_NONE); C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR); C3D_AlphaTest(true,GPU_GREATER,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE_MINUS_SRC_ALPHA);
}
C3D_RenderTarget *C2D_CreateScreenTarget(gfxScreen_t screen,gfx3dSide_t side)
{
    C3D_RenderTarget *target=C3D_RenderTargetCreate(240,screen==GFX_TOP?400:320,GPU_RB_RGBA8,GPU_RB_DEPTH16);
    if(target) C3D_RenderTargetSetOutput(target,screen,side,0);
    return target;
}
void C2D_TargetClear(C3D_RenderTarget *target,u32 color)
{ if(target) C3D_RenderTargetClear(target,C3D_CLEAR_ALL,__builtin_bswap32(color),0); }
void C2D_SceneBegin(C3D_RenderTarget *target)
{
    gpuC2DFlush(); if(!target) return;
    C3D_FrameDrawOn(target); C2D_SceneSize(target->frameBuf.width,target->frameBuf.height,target->linked);
}
void C2D_SceneSize(u32 width,u32 height,bool tilt)
{ sceneWidth=tilt?height:width; sceneHeight=tilt?width:height; sceneTilt=tilt; }
/* View/projection transforms are baked into each vertex before it is queued.
 * Changing them does not change pending geometry, so sprites with distinct
 * matrices can share a draw when their texture and fragment state agree. */
void C2D_ViewReset(void) { Mtx_Identity(&view); }
void C2D_ViewRestore(const C3D_Mtx *matrix) { view=*matrix; }
void C2D_ViewTranslate(float x,float y) { Mtx_Translate(&view,x,y,0,true); }
void C2D_ViewScale(float x,float y) { Mtx_Scale(&view,x,y,1); }

/* Raw Citro3D draws can reuse Citro2D's prepared projection/vertex shader.
 * Keep that public path separate from our CPU-baked 2D vertex batches. */
bool gpuC2DTransform(float out[12])
{
    if(!vertices || sceneWidth<=0 || sceneHeight<=0) return false;
    for(unsigned i=0;i<2;i++) {
        out[4*i]=view.r[i].x; out[4*i+1]=view.r[i].y;
        out[4*i+2]=view.r[i].z; out[4*i+3]=view.r[i].w;
    }
    out[8]=sceneWidth; out[9]=sceneHeight; out[10]=sceneTilt?1.0f:0.0f; out[11]=0;
    return true;
}

static Vertex2D vertex(float x,float y,float depth,float u,float v,C2D_Tint tint)
{
    float tx=view.r[0].x*x+view.r[0].y*y+view.r[0].w;
    float ty=view.r[1].x*x+view.r[1].y*y+view.r[1].w;
    /* Citro2D's [-1,+1] scene depth, after its PICA projection and depth map,
     * is OpenGL clip Z unchanged. A [0,1] conversion would clip negative Z. */
    Vertex2D out={.u=u,.v=v,.z=depth,.blend=tint.blend};
    if(sceneTilt) { out.x=1-2*ty/sceneHeight; out.y=2*tx/sceneWidth-1; }
    else { out.x=2*tx/sceneWidth-1; out.y=1-2*ty/sceneHeight; }
    out.r=(tint.color&255)/255.f; out.g=((tint.color>>8)&255)/255.f;
    out.b=((tint.color>>16)&255)/255.f; out.a=(tint.color>>24)/255.f;
    return out;
}
static bool drawQuad(C3D_Tex *texture,float x,float y,float z,float width,float height,float left,float top,float right,float bottom,const C2D_ImageTint *tint)
{
    if(!vertices || !gpuTarget || sceneWidth<=0 || sceneHeight<=0) return false;
    if(used && (batchTexture!=texture || used+6>capacity)) gpuC2DFlush();
    batchTexture=texture;
    C2D_Tint plain={0xff000000,0};
    Vertex2D corners[4]={
        vertex(x,y,z,left,top,tint?tint->corners[0]:plain),
        vertex(x+width,y,z,right,top,tint?tint->corners[1]:plain),
        vertex(x,y+height,z,left,bottom,tint?tint->corners[2]:plain),
        vertex(x+width,y+height,z,right,bottom,tint?tint->corners[3]:plain)};
    const int indices[]={0,2,1,1,2,3};
    for(int i=0;i<6;i++) vertices[used++]=corners[indices[i]];
    return true;
}
bool C2D_DrawImageAt(C2D_Image image,float x,float y,float depth,const C2D_ImageTint *tint,float scaleX,float scaleY)
{
    if(!image.tex || !image.subtex) return false;
    const Tex3DS_SubTexture *sub=image.subtex;
    float left=sub->left,right=sub->right,top=sub->top,bottom=sub->bottom;
    if(scaleX<0) { float tmp=left; left=right; right=tmp; }
    if(scaleY<0) { float tmp=top; top=bottom; bottom=tmp; }
    return drawQuad(image.tex,x,y,depth,fabsf(scaleX)*sub->width,fabsf(scaleY)*sub->height,left,top,right,bottom,tint);
}
bool C2D_DrawRectangle(float x,float y,float z,float width,float height,u32 tl,u32 tr,u32 bl,u32 br)
{
    C2D_ImageTint tint={.corners={{tl,1},{tr,1},{bl,1},{br,1}}};
    return drawQuad(NULL,x,y,z,width,height,0,0,1,1,&tint);
}
