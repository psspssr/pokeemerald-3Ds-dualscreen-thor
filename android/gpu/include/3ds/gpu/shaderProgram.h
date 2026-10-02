#pragma once
#include <3ds/gpu/shbin.h>
typedef struct { DVLE_s *dvle; } shaderInstance_s;
typedef struct { shaderInstance_s *vertexShader, *geometryShader; } shaderProgram_s;
Result shaderProgramInit(shaderProgram_s *program);
Result shaderProgramSetVsh(shaderProgram_s *program, DVLE_s *dvle);
Result shaderProgramFree(shaderProgram_s *program);
s8 shaderInstanceGetUniformLocation(shaderInstance_s *shader, const char *name);
