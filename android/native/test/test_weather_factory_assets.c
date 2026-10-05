#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef uint32_t u32;
#define ARRAY_COUNT(a) (sizeof(a)/sizeof(*(a)))
#define RGB2(r,g,b) ((r)|((g)<<5)|((b)<<10))
#define PLTT_ID(n) ((n)*16)
#define BG_PLTT_ID(n) PLTT_ID(n)
#define PLTT_SIZE_4BPP 32
#define PALNUM_FADE_TEXT 14
enum { COLOR_MAP_NONE, COLOR_MAP_DARK_CONTRAST, COLOR_MAP_CONTRAST };
#include "weather_factory_data.inc"
#ifdef PORT_BRIDGE
static const u16 sDroughtWeatherColors[6][0x1000];
static const u16 sPokeballGray_Pal[1], sInterface_Pal[1], sMonPicBg_Gfx[1];
/* This external declaration has a real fixed extent in graphics.h; it must
 * not be misclassified as the one-element private INCBIN declarations. */
static const u16 gFrontierFactoryMenu_Gfx[34*32/2];
static unsigned droughtResolves;
static const void *Port_ResolveAssetPointer(const void *ptr)
{
    if (ptr == sDroughtWeatherColors) { droughtResolves++; return droughtData; }
    if (ptr == sPokeballGray_Pal) return grayData;
    if (ptr == sInterface_Pal) return interfaceData;
    if (ptr == sMonPicBg_Gfx) return pictureData;
    if (ptr == gFrontierFactoryMenu_Gfx) return menuData;
    return ptr;
}
u32 Port_GetAssetSizeExact(const void *ptr)
{
    if (ptr == gFrontierFactoryMenu_Gfx) return sizeof(menuData);
    if (ptr == sMonPicBg_Gfx) return sizeof(pictureData);
    return 0;
}
static bool Port_LoadAssetPointerToBufferSized(const void *ptr, void *dest, u32 size)
{
    const void *data=Port_ResolveAssetPointer(ptr);
    if (data == ptr) return false;
    assert(size <= Port_GetAssetSizeExact(ptr));
    memcpy(dest, data, size); return true;
}
static bool Port_LoadAssetPointerToBuffer(const void *ptr, void *dest, u32 size)
{ return Port_LoadAssetPointerToBufferSized(ptr,dest,size); }
#else
#define sDroughtWeatherColors (*(const u16 (*)[6][0x1000])droughtData)
#define sPokeballGray_Pal grayData
#define sInterface_Pal interfaceData
#define sMonPicBg_Gfx pictureData
#define gFrontierFactoryMenu_Gfx menuData
#endif
#include "weather_factory_bios.inc"
static void CpuCopy16(const void *src,void *dst,u32 bytes) { CpuSet(src,dst,bytes/2); }
#define CpuFastCopy(src,dst,bytes) memcpy(dst,src,bytes)
static struct { u8 contrastColorMapSpritePalIndex, contrastColorMaps[19][32], darkenedContrastColorMaps[19][32]; } weather;
static typeof(weather) *gWeatherPtr=&weather;
static u8 types[32];
static const u8 *sPaletteColorMapTypes=types;
static u16 gPlttBufferFaded[512],gPlttBufferUnfaded[512];
static u16 blend(u16 color,u16 dest,u8 amount)
{
    int r=color&31,g=(color>>5)&31,b=(color>>10)&31;
    r+=(((dest&31)-r)*amount)>>4; g+=((((dest>>5)&31)-g)*amount)>>4; b+=((((dest>>10)&31)-b)*amount)>>4;
    return RGB2(r,g,b);
}
static void BlendPalette(u16 offset,u16 count,u8 amount,u16 color)
{ for(unsigned i=offset;i<offset+count;i++) gPlttBufferFaded[i]=blend(gPlttBufferUnfaded[i],color,amount); }
static unsigned fadeCalls;
static void BeginNormalPaletteFade(u32 mask,s8 delay,u8 from,u8 to,u16 color)
{ assert(mask==(1u<<PALNUM_FADE_TEXT) && delay==0 && ((from==0 && to==16)||(from==16 && to==0))); assert(color==interfaceData[5]); fadeCalls++; }
static u8 *sSelectMenuTilesetBuffer,*sSelectMonPicBgTilesetBuffer,*sSwapMenuTilesetBuffer,*sSwapMonPicBgTilesetBuffer;
static size_t allocationBytes[2],allocationCount,loadedBytes[4];
static u8 bg1[1088],bg3[96];
static void *Alloc(size_t size) { assert(allocationCount<2); allocationBytes[allocationCount++]=size; void *p=malloc(size); assert(p); memset(p,0xa5,size); return p; }
static void *AllocZeroed(size_t size) { void *p=Alloc(size); memset(p,0,size); return p; }
static void LoadBgTiles(u8 bg,const void *src,u32 size,u16 offset)
{ assert((bg==1||bg==3) && offset==0); assert(size <= (bg==1?sizeof(bg1):sizeof(bg3))); loadedBytes[bg]=size; memcpy(bg==1?bg1:bg3,src,size); }
#include "weather_factory_helpers.inc"
static void fillColors(unsigned first)
{
    for(unsigned i=0;i<512;i++) {
        unsigned index=(first+i)%4096;
        gPlttBufferUnfaded[i]=((index&15)<<1)|((index&0xf0)<<2)|((index&0xf00)<<3);
        gPlttBufferFaded[i]=0xa5a5;
    }
    memset(types,COLOR_MAP_CONTRAST,sizeof(types));
}
static void weatherTest(bool blended)
{
    for(unsigned row=0;row<6;row++) for(unsigned first=0;first<4096;first+=512) {
        fillColors(first);
        if(blended) ApplyDroughtColorMapWithBlend(-(s8)row-1,0,0x1234);
        else ApplyColorMap(0,32,-(s8)row-1);
        for(unsigned i=0;i<512;i++) assert(gPlttBufferFaded[i]==droughtData[row*4096+first+i]);
    }
    for(unsigned amount=8;amount<=16;amount+=8) {
        fillColors(2048); types[3]=COLOR_MAP_NONE;
        if(blended) {
            ApplyDroughtColorMapWithBlend(-6,amount,0x4567);
            for(unsigned i=0;i<512;i++) {
                u16 color=i/16==3?gPlttBufferUnfaded[i]:droughtData[5*4096+2048+i];
                assert(gPlttBufferFaded[i]==blend(color,0x4567,amount));
            }
        } else {
            ApplyColorMap(2,3,-6);
            for(unsigned i=0;i<512;i++) assert(gPlttBufferFaded[i] ==
                (i<32||i>=80?0xa5a5:i/16==3?gPlttBufferUnfaded[i]:droughtData[5*4096+2048+i]));
        }
    }
    fillColors(0);
#ifdef PORT_BRIDGE
    unsigned before=droughtResolves;
#endif
    ApplyColorMap(0,32,0); assert(!memcmp(gPlttBufferFaded,gPlttBufferUnfaded,sizeof(gPlttBufferFaded)));
#ifdef PORT_BRIDGE
    assert(droughtResolves==before);
#endif
    printf("PASS actual drought%s: all6x4096 lookups, blend, palette exclusions and bounds\n",blended?" blend":" map");
}
static void factoryBuffers(bool select)
{
    allocationCount=0; memset(loadedBytes,0,sizeof(loadedBytes)); memset(bg1,0xa5,sizeof(bg1)); memset(bg3,0xa5,sizeof(bg3));
    if(select) FactorySelectBuffers(); else FactorySwapBuffers();
    assert(allocationCount==2 && allocationBytes[0]>=sizeof(menuData) && allocationBytes[1]>=sizeof(pictureData));
    assert(loadedBytes[1]==sizeof(menuData) && loadedBytes[3]==sizeof(pictureData));
    assert(!memcmp(bg1,menuData,sizeof(bg1)) && !memcmp(bg3,pictureData,sizeof(bg3)));
    free(select?sSelectMenuTilesetBuffer:sSwapMenuTilesetBuffer);
    free(select?sSelectMonPicBgTilesetBuffer:sSwapMonPicBgTilesetBuffer);
    printf("PASS actual Factory %s allocation/CpuSet/LoadBgTiles:1088menu bytes and96picture bytes\n",select?"select":"swap");
}
int main(int argc,char **argv)
{
    assert(argc==2 && sizeof(menuData)==1088 && sizeof(pictureData)==96 && sizeof(grayData)==32);
    if(!strcmp(argv[1],"all")||!strcmp(argv[1],"weather")) weatherTest(false);
    if(!strcmp(argv[1],"all")||!strcmp(argv[1],"weather-blend")) weatherTest(true);
    if(!strcmp(argv[1],"all")||!strcmp(argv[1],"factory-fade")) {
        FactoryFadeConsumers(); assert(fadeCalls==2 && gPlttBufferFaded[BG_PLTT_ID(PALNUM_FADE_TEXT)+2]==interfaceData[5]);
        assert(interfaceData[5]==0x79d4); puts("PASS all3 actual Factory fade consumers: original GBA interface[5] color, no adjacent-array read");
    }
    if(!strcmp(argv[1],"all")||!strcmp(argv[1],"factory-select")) factoryBuffers(true);
    if(!strcmp(argv[1],"all")||!strcmp(argv[1],"factory-swap")) factoryBuffers(false);
    return 0;
}
