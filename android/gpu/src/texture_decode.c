/* Pure CPU PICA texture conversion, also compiled by the host regression tests. */
#include <3ds/gpu/enums.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

unsigned gpuPixelBytes(GPU_TEXCOLOR format)
{
    static const unsigned bytes[]={4,3,2,2,2,2,2,1,1,1,0,0,0,0};
    return (unsigned)format<sizeof(bytes)/sizeof(*bytes)?bytes[format]:0;
}

size_t gpuTextureSize(unsigned width,unsigned height,GPU_TEXCOLOR format)
{
    if (format==GPU_L4 || format==GPU_A4 || format==GPU_ETC1) return (size_t)width*height/2;
    if (format==GPU_ETC1A4) return (size_t)width*height;
    return (size_t)width*height*gpuPixelBytes(format);
}

static unsigned expand5(unsigned n) { return (n<<3)|(n>>2); }
static unsigned expand6(unsigned n) { return (n<<2)|(n>>4); }

void gpuDecodePixel(const unsigned char *source,GPU_TEXCOLOR format,unsigned char *out)
{
    unsigned v=0;
    if (gpuPixelBytes(format)==2) v=source[0]|((unsigned)source[1]<<8);
    out[3]=255;
    switch(format) {
    case GPU_RGBA8: out[0]=source[3]; out[1]=source[2]; out[2]=source[1]; out[3]=source[0]; break;
    case GPU_RGB8: out[0]=source[2]; out[1]=source[1]; out[2]=source[0]; break;
    case GPU_RGBA5551: out[0]=expand5(v>>11); out[1]=expand5((v>>6)&31); out[2]=expand5((v>>1)&31); out[3]=(v&1)?255:0; break;
    case GPU_RGB565: out[0]=expand5(v>>11); out[1]=expand6((v>>5)&63); out[2]=expand5(v&31); break;
    case GPU_RGBA4: out[0]=((v>>12)&15)*17; out[1]=((v>>8)&15)*17; out[2]=((v>>4)&15)*17; out[3]=(v&15)*17; break;
    case GPU_LA8: out[0]=out[1]=out[2]=source[1]; out[3]=source[0]; break;
    case GPU_HILO8: out[0]=source[1]; out[1]=source[0]; out[2]=0; break;
    case GPU_L8: out[0]=out[1]=out[2]=source[0]; break;
    case GPU_A8: out[0]=out[1]=out[2]=255; out[3]=source[0]; break;
    case GPU_LA4: out[0]=out[1]=out[2]=(source[0]>>4)*17; out[3]=(source[0]&15)*17; break;
    default: memset(out,0,4); break;
    }
}

bool gpuDecodeTexture(const void *source,unsigned char *rgba,unsigned width,unsigned height,GPU_TEXCOLOR format)
{
    if (!source || !rgba || width%8 || height%8 || format>=GPU_ETC1) return false;
    const unsigned char *bytes=source;
    unsigned bpp=gpuPixelBytes(format);
    for(unsigned y=0;y<height;y++) for(unsigned x=0;x<width;x++) {
        unsigned morton=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
        unsigned index=((y/8)*(width/8)+x/8)*64+morton;
        /* PICA's first tile row is the top; OpenGL's first upload row is the bottom. */
        unsigned char *pixel=rgba+4*((height-1-y)*width+x);
        if(format==GPU_L4 || format==GPU_A4) {
            unsigned n=((bytes[index/2]>>((index&1)*4))&15)*17;
            pixel[0]=pixel[1]=pixel[2]=format==GPU_L4?n:255;
            pixel[3]=format==GPU_A4?n:255;
        } else gpuDecodePixel(bytes+index*bpp,format,pixel);
    }
    return true;
}
