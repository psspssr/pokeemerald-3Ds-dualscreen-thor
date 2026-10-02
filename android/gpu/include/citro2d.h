#pragma once
#include <citro3d.h>
#include <tex3ds.h>
#define C2D_DEFAULT_MAX_OBJECTS 4096
typedef struct { C3D_Tex *tex; const Tex3DS_SubTexture *subtex; } C2D_Image;
typedef enum { C2D_TopLeft, C2D_TopRight, C2D_BotLeft, C2D_BotRight } C2D_Corner;
typedef struct { u32 color; float blend; } C2D_Tint;
typedef struct { C2D_Tint corners[4]; } C2D_ImageTint;
static inline u32 C2D_Color32(u8 r,u8 g,u8 b,u8 a) { return r|((u32)g<<8)|((u32)b<<16)|((u32)a<<24); }
static inline u32 C2D_Color32f(float r,float g,float b,float a) { return C2D_Color32(r*255,g*255,b*255,a*255); }
static inline void C2D_SetImageTint(C2D_ImageTint *t,C2D_Corner c,u32 color,float blend) { t->corners[c]=(C2D_Tint){color,blend}; }
static inline void C2D_PlainImageTint(C2D_ImageTint *t,u32 c,float blend) { for(int i=0;i<4;i++) C2D_SetImageTint(t,(C2D_Corner)i,c,blend); }
static inline void C2D_AlphaImageTint(C2D_ImageTint *t,float a) { C2D_PlainImageTint(t,C2D_Color32f(1,1,1,a),0); }
static inline void C2D_TopImageTint(C2D_ImageTint *t,u32 c,float b) { C2D_SetImageTint(t,C2D_TopLeft,c,b); C2D_SetImageTint(t,C2D_TopRight,c,b); }
static inline void C2D_BottomImageTint(C2D_ImageTint *t,u32 c,float b) { C2D_SetImageTint(t,C2D_BotLeft,c,b); C2D_SetImageTint(t,C2D_BotRight,c,b); }
bool C2D_Init(size_t maxObjects);
void C2D_Fini(void);
void C2D_Prepare(void);
void C2D_Flush(void);
C3D_RenderTarget *C2D_CreateScreenTarget(gfxScreen_t screen,gfx3dSide_t side);
void C2D_TargetClear(C3D_RenderTarget *target,u32 color);
void C2D_SceneBegin(C3D_RenderTarget *target);
void C2D_SceneSize(u32 width,u32 height,bool tilt);
void C2D_ViewReset(void);
void C2D_ViewRestore(const C3D_Mtx *matrix);
void C2D_ViewTranslate(float x,float y);
void C2D_ViewScale(float x,float y);
bool C2D_DrawImageAt(C2D_Image image,float x,float y,float depth,const C2D_ImageTint *tint,float scaleX,float scaleY);
bool C2D_DrawRectangle(float x,float y,float z,float width,float height,u32 topLeft,u32 topRight,u32 bottomLeft,u32 bottomRight);
static inline bool C2D_DrawRectSolid(float x,float y,float z,float w,float h,u32 color) { return C2D_DrawRectangle(x,y,z,w,h,color,color,color,color); }
