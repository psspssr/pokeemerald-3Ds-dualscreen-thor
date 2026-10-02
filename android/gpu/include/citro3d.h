#pragma once
#include <3ds.h>
#include <c3d/maths.h>
#include <c3d/attribs.h>
#include <c3d/buffers.h>
#include <c3d/texture.h>
#include <c3d/texenv.h>
#include <c3d/effect.h>
#include <c3d/renderqueue.h>
#include <c3d/uniforms.h>
#include <c3d/mtxstack.h>
#define C3D_DEFAULT_CMDBUF_SIZE 0x40000
bool C3D_Init(size_t commandBufferSize);
void C3D_Fini(void);
void C3D_BindProgram(shaderProgram_s *program);
void C3D_DrawArrays(GPU_Primitive_t primitive, int first, int count);
void C3D_SetScissor(GPU_SCISSORMODE mode, u32 left, u32 bottom, u32 right, u32 top);
void C3D_SetViewport(u32 x, u32 y, u32 width, u32 height);
