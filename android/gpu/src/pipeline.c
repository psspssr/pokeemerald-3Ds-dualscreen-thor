#include "gpu_internal.h"
#include <stdarg.h>
#include <stdio.h>

typedef struct {
    C3D_TexEnv stages[6];
    DVLE_s *vertex;
    unsigned alphaFunc;
    bool citro2d,alpha;
} ProgramKey;
typedef struct {
    ProgramKey key;
    GLuint program;
    GLint uniforms,colors,alphaRef;
    C3D_FVec uniformValues[C3D_FVUNIF_COUNT];
    u32 colorValues[6];
    int alphaValue;
    unsigned samplerMask;
    bool uniformsUploaded,colorsUploaded,alphaUploaded;
    unsigned long used;
} CachedProgram;
static CachedProgram programs[96];
static unsigned long sequence;
static GLuint whiteTexture;

static const char *vertex2d=
    "#version 300 es\nprecision highp float;\n"
    "layout(location=0) in vec3 position; layout(location=1) in vec2 uv;"
    "layout(location=2) in vec4 color; layout(location=3) in float blend;\n"
    "out vec4 v_color;out vec4 v_tex0;out vec4 v_tex1;out vec4 v_tex2;out float v_blend;\n"
    "void main(){gl_Position=vec4(position,1);v_color=color;v_tex0=vec4(uv,0,1);"
    "v_tex1=vec4(0);v_tex2=vec4(0);v_blend=blend;}\n";

static void append(char *buffer,size_t capacity,size_t *used,const char *format,...)
{
    if(*used>=capacity) return;
    va_list args; va_start(args,format);
    int length=vsnprintf(buffer+*used,capacity-*used,format,args); va_end(args);
    if(length>0) *used+=(size_t)length;
}
static void sourceExpr(char out[64],unsigned source,int stage)
{
    switch(source) {
    case GPU_PRIMARY_COLOR: case GPU_FRAGMENT_PRIMARY_COLOR: snprintf(out,64,"v_color"); break;
    case GPU_TEXTURE0: case GPU_TEXTURE1: case GPU_TEXTURE2: snprintf(out,64,"t%u",source-GPU_TEXTURE0); break;
    case GPU_CONSTANT: snprintf(out,64,"c[%d]",stage); break;
    case GPU_PREVIOUS: snprintf(out,64,"p"); break;
    default: snprintf(out,64,"vec4(0.0)"); break;
    }
}
static void operandExpr(char out[160],unsigned source,unsigned operand,bool alpha,int stage)
{
    char src[64]; sourceExpr(src,source,stage);
    const char *component;
    if(alpha) component=(const char *[]) {"a","a","r","r","g","g","b","b"}[operand&7];
    else switch(operand&14) {
        case 2: component="aaa"; break; case 4: component="rrr"; break;
        case 8: component="ggg"; break; case 12: component="bbb"; break;
        default: component="rgb"; break;
    }
    if(operand&1) snprintf(out,160,"(1.0-(%s).%s)",src,component);
    else snprintf(out,160,"(%s).%s",src,component);
}
static void combineExpr(char out[1200],char args[3][160],unsigned func,bool alpha)
{
    const char *a=args[0],*b=args[1],*c=args[2];
    switch(func) {
    case GPU_REPLACE: snprintf(out,1200,"%s",a); break;
    case GPU_MODULATE: snprintf(out,1200,"(%s*%s)",a,b); break;
    case GPU_ADD: snprintf(out,1200,"(%s+%s)",a,b); break;
    case GPU_ADD_SIGNED: snprintf(out,1200,"(%s+%s-0.5)",a,b); break;
    case GPU_INTERPOLATE: snprintf(out,1200,"(%s*%s+%s*(1.0-%s))",a,c,b,c); break;
    case GPU_SUBTRACT: snprintf(out,1200,"(%s-%s)",a,b); break;
    case GPU_MULTIPLY_ADD: snprintf(out,1200,"(%s*%s+%s)",a,b,c); break;
    case GPU_ADD_MULTIPLY: snprintf(out,1200,"(min(%s+%s,1.0)*%s)",a,b,c); break;
    case GPU_DOT3_RGB: case GPU_DOT3_RGBA:
        snprintf(out,1200,alpha?"(4.0*(%s-0.5)*(%s-0.5))":"vec3(4.0*dot(%s-0.5,%s-0.5))",a,b); break;
    default: snprintf(out,1200,alpha?"0.0":"vec3(0.0)"); break;
    }
}
static unsigned combineInputs(unsigned function)
{
    switch(function) {
    case GPU_REPLACE: return 1;
    case GPU_MODULATE: case GPU_ADD: case GPU_ADD_SIGNED: case GPU_SUBTRACT:
    case GPU_DOT3_RGB: case GPU_DOT3_RGBA: return 2;
    case GPU_INTERPOLATE: case GPU_MULTIPLY_ADD: case GPU_ADD_MULTIPLY: return 3;
    default: return 0;
    }
}
static unsigned programSamplers(const ProgramKey *key)
{
    unsigned mask=key->citro2d?1:0;
    for(int i=key->citro2d?4:0;i<6;i++) {
        const C3D_TexEnv *env=&key->stages[i];
        unsigned sources[]={env->srcRgb,env->srcAlpha};
        unsigned count[]={combineInputs(env->funcRgb),combineInputs(env->funcAlpha)};
        for(int mode=0;mode<2;mode++) for(unsigned j=0;j<count[mode];j++) {
            unsigned source=(sources[mode]>>(j*4))&15;
            if(source>=GPU_TEXTURE0 && source<=GPU_TEXTURE2) mask|=1u<<(source-GPU_TEXTURE0);
        }
    }
    return mask;
}
static GLuint makeProgram(ProgramKey *key)
{
    char code[32768]; size_t used=0;
    append(code,sizeof(code),&used,"#version 300 es\nprecision highp float;\n"
        "in vec4 v_color;in vec4 v_tex0;in vec4 v_tex1;in vec4 v_tex2;\n"
        "uniform sampler2D tex0;uniform sampler2D tex1;uniform sampler2D tex2;"
        "uniform vec4 c[6];uniform float alphaRef;out vec4 outputColor;\n");
    if(key->citro2d) append(code,sizeof(code),&used,"in float v_blend;\n");
    append(code,sizeof(code),&used,"void main(){vec4 p=v_color;\n");
    /* Do not rely on the driver to remove unused texture() expressions:
     * some retain those fetches, samplers and texture bindings as active. */
    unsigned samplers=programSamplers(key);
    for(unsigned unit=0;unit<3;unit++) if(samplers&(1u<<unit))
        append(code,sizeof(code),&used,"vec4 t%u=texture(tex%u,v_tex%u.xy);\n",unit,unit,unit);
    if(key->citro2d) append(code,sizeof(code),&used,"p=vec4(mix(t0.rgb,v_color.rgb,v_blend),t0.a*v_color.a);\n");
    for(int i=key->citro2d?4:0;i<6;i++) {
        C3D_TexEnv *env=&key->stages[i];
        char rgbArgs[3][160],alphaArgs[3][160],rgb[1200],alpha[1200];
        for(int j=0;j<3;j++) {
            operandExpr(rgbArgs[j],(env->srcRgb>>(j*4))&15,(env->opRgb>>(j*4))&15,false,i);
            operandExpr(alphaArgs[j],(env->srcAlpha>>(j*4))&15,(env->opAlpha>>(j*4))&15,true,i);
        }
        combineExpr(rgb,rgbArgs,env->funcRgb,false); combineExpr(alpha,alphaArgs,env->funcAlpha,true);
        append(code,sizeof(code),&used,"p=clamp(vec4((%s)*%.1f,(%s)*%.1f),0.0,1.0);\n",rgb,(double)(1u<<(env->scaleRgb&3)),alpha,(double)(1u<<(env->scaleAlpha&3)));
    }
    if(key->alpha) {
        static const char *checks[]={"false","true","p.a==alphaRef","p.a!=alphaRef","p.a<alphaRef","p.a<=alphaRef","p.a>alphaRef","p.a>=alphaRef"};
        append(code,sizeof(code),&used,"if(!(%s))discard;\n",checks[key->alphaFunc&7]);
    }
    append(code,sizeof(code),&used,"outputColor=p;}\n");
    if(used>=sizeof(code)) { GPU_LOG("fragment shader too large"); return 0; }
    const char *source=key->citro2d?vertex2d:key->vertex->source;
    GLuint vertex=gpuCompile(GL_VERTEX_SHADER,source),fragment=gpuCompile(GL_FRAGMENT_SHADER,code);
    GLuint result=gpuLink(vertex,fragment); glDeleteShader(vertex); glDeleteShader(fragment); return result;
}

GLuint gpuUseProgram(bool citro2d)
{
    ProgramKey key={0};
    memcpy(key.stages,gpuEnvs,sizeof(gpuEnvs));
    for(int i=0;i<6;i++) { key.stages[i].color=0; if(citro2d && i<4) memset(&key.stages[i],0,sizeof(C3D_TexEnv)); }
    key.citro2d=citro2d; key.alpha=gpuAlphaEnabled; key.alphaFunc=gpuAlphaEnabled?gpuAlphaFunc:0;
    key.vertex=citro2d?NULL:gpuProgram->vertexShader->dvle;
    CachedProgram *cache=NULL,*oldest=&programs[0];
    for(unsigned i=0;i<sizeof(programs)/sizeof(*programs);i++) {
        if(programs[i].program && !memcmp(&key,&programs[i].key,sizeof(key))) { cache=&programs[i]; break; }
        if(programs[i].used<oldest->used) oldest=&programs[i];
    }
    if(!cache) {
        cache=oldest;
        if(cache->program) glDeleteProgram(cache->program);
        memset(cache,0,sizeof(*cache)); cache->key=key; cache->program=makeProgram(&key);
        if(!cache->program) return 0;
        glUseProgram(cache->program);
        cache->uniforms=glGetUniformLocation(cache->program,"u"); cache->colors=glGetUniformLocation(cache->program,"c");
        cache->alphaRef=glGetUniformLocation(cache->program,"alphaRef");
        const char *samplers[]={"tex0","tex1","tex2"};
        for(int unit=0;unit<3;unit++) {
            GLint location=glGetUniformLocation(cache->program,samplers[unit]);
            if(location>=0) { glUniform1i(location,unit); cache->samplerMask|=1u<<unit; }
        }
    }
    cache->used=++sequence; glUseProgram(cache->program);
    if(cache->colors>=0) {
        bool changed=!cache->colorsUploaded;
        for(int i=0;i<6;i++) if(cache->colorValues[i]!=gpuEnvs[i].color) changed=true;
        if(changed) {
            float colors[24];
            for(int i=0;i<6;i++) {
                cache->colorValues[i]=gpuEnvs[i].color;
                for(int j=0;j<4;j++) colors[i*4+j]=((gpuEnvs[i].color>>(j*8))&255)/255.f;
            }
            glUniform4fv(cache->colors,6,colors); cache->colorsUploaded=true;
        }
    }
    if(cache->alphaRef>=0 && (!cache->alphaUploaded || cache->alphaValue!=gpuAlphaRef)) {
        glUniform1f(cache->alphaRef,gpuAlphaRef/255.f);
        cache->alphaValue=gpuAlphaRef; cache->alphaUploaded=true;
    }
    if(!citro2d && cache->uniforms>=0) {
        /* Upstream often changes only a model/lighting register between
         * draws. Compare actual values, including direct register writes,
         * against this linked program's last upload. A prefix upload avoids
         * assuming adjacent array elements have consecutive GL locations. */
        unsigned count=C3D_FVUNIF_COUNT;
        if(cache->uniformsUploaded) {
            if(!memcmp(cache->uniformValues,C3D_FVUnif[0],sizeof(cache->uniformValues))) count=0;
            else while(count && !memcmp(&cache->uniformValues[count-1],&C3D_FVUnif[0][count-1],sizeof(C3D_FVec))) --count;
        }
        if(count) {
            float uniforms[C3D_FVUNIF_COUNT*4];
            for(unsigned i=0;i<count;i++) { C3D_FVec v=C3D_FVUnif[0][i]; uniforms[i*4]=v.x; uniforms[i*4+1]=v.y; uniforms[i*4+2]=v.z; uniforms[i*4+3]=v.w; }
            glUniform4fv(cache->uniforms,count,uniforms);
            memcpy(cache->uniformValues,C3D_FVUnif[0],count*sizeof(C3D_FVec)); cache->uniformsUploaded=true;
        }
    }
    for(int unit=0;unit<3;unit++) {
        /* A sampler optimized out of this linked shader cannot consume a
         * texture. Keep checking live CPU bytes for every active sampler. */
        if(!(cache->samplerMask&(1u<<unit))) continue;
        glActiveTexture(GL_TEXTURE0+unit);
        if(GPU_BOUND_TEXTURES[unit]) {
            if(!gpuTextureId(GPU_BOUND_TEXTURES[unit])) return 0;
        } else {
            if(!whiteTexture) {
                const unsigned char white[4]={255,255,255,255}; glGenTextures(1,&whiteTexture); glBindTexture(GL_TEXTURE_2D,whiteTexture);
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            } else glBindTexture(GL_TEXTURE_2D,whiteTexture);
        }
    }
    return cache->program;
}
