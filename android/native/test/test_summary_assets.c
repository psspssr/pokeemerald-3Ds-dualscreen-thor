/* The real helper bodies are extracted from generated pokemon_summary_screen.c.
 * A one-u16 external stub must never be indexed as though it were its payload. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint8_t bool8;

struct TilemapCtrl {
    const u16 *gfx;
    u16 field_4;
    u8 field_6, field_7, field_8, field_9;
};

#define ROW(a) a,a+1,a+2,a+3,a+4,a+5,a+6,a+7,a+8,a+9
#define CANCEL_DATA ROW(0x100),ROW(0x200),ROW(0x300),ROW(0x400),ROW(0x500),ROW(0x600),0,0,0,0
#define SLIDING_DATA ROW(0x710),ROW(0x720),ROW(0x730),ROW(0x740),ROW(0x750),ROW(0x760),ROW(0x770),ROW(0x780)
static const u16 cancelPayload[]={CANCEL_DATA};
static const u16 slidingPayload[]={SLIDING_DATA};
#ifdef PORT_BRIDGE
static const u16 gSummaryScreen_MoveEffect_Cancel_Tilemap[1]={0};
static const u16 slidingAsset[1]={0};
#else
static const u16 gSummaryScreen_MoveEffect_Cancel_Tilemap[]={CANCEL_DATA};
static const u16 slidingAsset[]={SLIDING_DATA};
#endif
static unsigned resolves, requested;
static bool8 missing;
static const void *Port_ResolveAssetPointer(const void *base)
{
    resolves++;
    if(base==gSummaryScreen_MoveEffect_Cancel_Tilemap) return cancelPayload;
    assert(base==slidingAsset);
    return slidingPayload;
}
static const void *Port_ResolveAssetPointerSized(const void *base, unsigned bytes)
{
    requested=bytes;
    const void *payload=Port_ResolveAssetPointer(base);
    assert(bytes <= (base==gSummaryScreen_MoveEffect_Cancel_Tilemap ? sizeof(cancelPayload) : sizeof(slidingPayload)));
    return missing ? NULL : payload;
}
static void *Alloc(unsigned size) { void *p=malloc(size); assert(p); return p; }
static void Free(void *p) { free(p); }
static void CpuFill16(u16 value,u16 *out,unsigned bytes)
{ for(unsigned i=0;i<bytes/2;i++) out[i]=value; }
static void CpuCopy16(const u16 *src,u16 *dst,unsigned bytes)
{
#ifdef PORT_BRIDGE
    /* Like the bridge copy path, a base asset pointer can resolve. Interior
     * addresses must already point into its payload, never the tiny stub. */
    if(src==gSummaryScreen_MoveEffect_Cancel_Tilemap || src==slidingAsset)
        src=Port_ResolveAssetPointer(src);
#endif
    memcpy(dst,src,bytes);
}

#include "helpers.inc"

static void fiveRows(void)
{
    for(unsigned remove=0;remove<2;remove++) for(unsigned palette=1;palette<=3;palette+=2) {
        u16 destination[2048];
        for(unsigned i=0;i<2048;i++) destination[i]=0xDEAD;
        resolves=0;
        TilemapFiveMovesDisplay(destination,palette,remove);
        for(unsigned i=0;i<2048;i++) {
            unsigned row=i/32,column=i%32;
            u16 expected=0xDEAD;
            if(row>=43 && row<=45 && column>=10 && column<30) {
                unsigned sourceRow=remove?(row==43?1:2):(row==45?1:0);
                expected=cancelPayload[sourceRow*20+column-10]+palette*0x1000;
            }
            assert(destination[i]==expected);
        }
#ifdef PORT_BRIDGE
        assert(resolves==1);
        assert(requested==60*sizeof(u16));
#else
        assert(resolves==0);
#endif
    }
    puts("PASS five-row open and normal four-row restore use the real tilemap, with unchanged footprint/palette");
}

static void slidingPanels(void)
{
    const struct TilemapCtrl controls={slidingAsset,7,10,7,3,45};
    for(unsigned reverse=0;reverse<2;reverse++) for(unsigned hidden=0;hidden<=10;hidden++) {
        u16 destination[2048];
        for(unsigned i=0;i<2048;i++) destination[i]=0xDEAD;
        resolves=0;
        ChangeTilemap(&controls,destination,hidden,reverse);
        for(unsigned i=0;i<2048;i++) {
            unsigned row=i/32,column=i%32;
            u16 expected=0xDEAD;
            if(row>=45 && row<52 && column>=3 && column<13) {
                unsigned x=column-3;
                expected=7;
                if(reverse && x>=hidden) expected=slidingPayload[(row-45)*10+x-hidden];
                if(!reverse && x<10-hidden) expected=slidingPayload[(row-45)*10+x+hidden];
            }
            assert(destination[i]==expected);
        }
#ifdef PORT_BRIDGE
        assert(resolves==1 && requested==10*7*sizeof(u16));
#else
        assert(resolves==0);
#endif
    }
    puts("PASS left/right sliding panels resolve once before all row offsets, including hidden endpoints");
}

int main(int argc,char **argv)
{
    /* Keep the mock resolver type-checked in an embedded-data build too. */
    (void)Port_ResolveAssetPointer;
    (void)Port_ResolveAssetPointerSized;
    if(argc<2 || strcmp(argv[1],"sliding")) fiveRows();
    slidingPanels();
#ifdef PORT_BRIDGE
    u16 intact[2048];
    for(unsigned i=0;i<2048;i++) intact[i]=0xDEAD;
    missing=1;
    TilemapFiveMovesDisplay(intact,3,0);
    const struct TilemapCtrl panel={slidingAsset,7,10,7,3,45};
    ChangeTilemap(&panel,intact,0,0);
    for(unsigned i=0;i<2048;i++) assert(intact[i]==0xDEAD);
    puts("PASS unavailable or undersized payload leaves existing tilemaps intact");
#endif
    puts("PASS summary assets under address/undefined-behavior sanitizers");
    return 0;
}
