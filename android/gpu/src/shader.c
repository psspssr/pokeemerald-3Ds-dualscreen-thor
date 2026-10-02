#include "gpu_internal.h"
#include <stdio.h>

DVLB_s *DVLB_ParseFile(u32 *data,u32 size)
{
    const unsigned char *bytes=(const unsigned char *)data;
    u32 length,count;
    if(!bytes || size<16 || memcmp(bytes,"CTRGLS1\0",8)) return NULL;
    memcpy(&length,bytes+8,4); memcpy(&count,bytes+12,4);
    if(count>96 || length<2 || length>1024*1024 || (size_t)16+count*68+length!=size) return NULL;
    const char *source=(const char *)(bytes+16+count*68);
    if(source[length-1] || memchr(source,0,length-1) || strncmp(source,"#version 300 es\n",16)) return NULL;
    DVLB_s *binary=calloc(1,sizeof(*binary));
    if(!binary) return NULL;
    binary->DVLE=calloc(1,sizeof(*binary->DVLE));
    if(!binary->DVLE) { free(binary); return NULL; }
    binary->numDVLE=1;
    DVLE_s *entry=binary->DVLE;
    entry->uniforms=calloc(count?count:1,sizeof(*entry->uniforms));
    entry->source=strdup(source);
    if(!entry->uniforms || !entry->source) { DVLB_Free(binary); return NULL; }
    entry->uniformCount=count;
    for(unsigned i=0;i<count;i++) {
        memcpy(&entry->uniforms[i],bytes+16+i*68,68);
        if(!memchr(entry->uniforms[i].name,0,64) || entry->uniforms[i].index>=96) { DVLB_Free(binary); return NULL; }
    }
    return binary;
}
void DVLB_Free(DVLB_s *binary)
{
    if(!binary) return;
    for(unsigned i=0;i<binary->numDVLE;i++) { free(binary->DVLE[i].source); free(binary->DVLE[i].uniforms); }
    free(binary->DVLE); free(binary);
}
Result shaderProgramInit(shaderProgram_s *program) { if(!program) return -1; memset(program,0,sizeof(*program)); return 0; }
Result shaderProgramSetVsh(shaderProgram_s *program,DVLE_s *entry)
{
    if(!program || !entry) return -1;
    if(!program->vertexShader) program->vertexShader=calloc(1,sizeof(*program->vertexShader));
    if(!program->vertexShader) return -1;
    program->vertexShader->dvle=entry; return 0;
}
Result shaderProgramFree(shaderProgram_s *program)
{
    if(!program) return -1;
    if(gpuProgram==program) gpuProgram=NULL;
    free(program->vertexShader); free(program->geometryShader); memset(program,0,sizeof(*program)); return 0;
}
s8 shaderInstanceGetUniformLocation(shaderInstance_s *shader,const char *name)
{
    if(!shader || !shader->dvle || !name) return -1;
    for(unsigned i=0;i<shader->dvle->uniformCount;i++)
        if(!strcmp(shader->dvle->uniforms[i].name,name)) return shader->dvle->uniforms[i].index;
    return -1;
}
GLuint gpuCompile(GLenum type,const char *source)
{
    GLuint shader=glCreateShader(type); GLint ok=0;
    glShaderSource(shader,1,&source,NULL); glCompileShader(shader); glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok) { char log[4096]; glGetShaderInfoLog(shader,sizeof(log),NULL,log); GPU_LOG("shader compile failed: %s",log); glDeleteShader(shader); return 0; }
    return shader;
}
GLuint gpuLink(GLuint vertex,GLuint fragment)
{
    if(!vertex || !fragment) return 0;
    GLuint program=glCreateProgram(); GLint ok=0;
    glAttachShader(program,vertex); glAttachShader(program,fragment); glLinkProgram(program); glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok) { char log[4096]; glGetProgramInfoLog(program,sizeof(log),NULL,log); GPU_LOG("shader link failed: %s",log); glDeleteProgram(program); return 0; }
    return program;
}
