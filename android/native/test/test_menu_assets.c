#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gba/types.h"
#include "gba/defines.h"
#include "sprite.h"

#define ARRAY_COUNT(a) (sizeof(a) / sizeof(*(a)))
#define OBJ_PLTT_OFFSET 256
#define BG_PLTT_ID(n) ((n) * 16)
#define MAX_DMA_REQUESTS 128
#define DMA_REQUEST_COPY32 1
#define DMA_REQUEST_COPY16 3

static u16 selectionColors[32], outputColors[512];
static u8 outlines[6][960], tileVram[65536];
#ifdef PORT_BRIDGE
static const u16 gPokenavConditionCancel_Pal[1];
/* Grouped INCBIN rows retain their declared 960-byte C stride. The actual
 * index also publishes each exact row pointer, but rejects oversized reads. */
static const u8 sMapPopUp_OutlineTable[6][960];
static const void *Port_ResolveAssetPointer(const void *ptr) __attribute__((unused));
static const void *Port_ResolveAssetPointer(const void *ptr)
{ return ptr == gPokenavConditionCancel_Pal ? selectionColors : ptr; }
static const void *Port_ResolveAssetPointerSized(const void *ptr, u32 size)
{
    if (ptr == gPokenavConditionCancel_Pal && size <= sizeof(selectionColors)) return selectionColors;
    for (unsigned row = 0; row < 6; row++)
        if (ptr == sMapPopUp_OutlineTable[row] && size <= sizeof(outlines) - row * sizeof(outlines[0]))
            return outlines[row];
    return ptr;
}
static bool Port_LoadAssetPointerToBufferSized(const void *ptr, void *dest, u32 size)
{
    const void *resolved = Port_ResolveAssetPointerSized(ptr, size);
    if (resolved == ptr) return false;
    memcpy(dest, resolved, size); return true;
}
static bool Port_LoadAssetPointerToBuffer(const void *ptr, void *dest, u32 size)
{ return Port_LoadAssetPointerToBufferSized(ptr, dest, size); }
#else
#define gPokenavConditionCancel_Pal selectionColors
#define sMapPopUp_OutlineTable outlines
#endif
#include "menu_bios.inc"
static const u32 sConditionPokeball_Gfx[1], sConditionPokeballPlaceholder_Gfx[1], gPokenavConditionCancel_Gfx[1];
static const struct OamData sOam_ConditionSelectionIcon = {0};
static const union AnimCmd *const sAnims_ConditionSelectionIcon[1] = {NULL};
const union AffineAnimCmd *const gDummySpriteAffineAnimTable[1] = {NULL};
void SpriteCallbackDummy(struct Sprite *sprite) { (void)sprite; }
static void LoadPalette(const void *src, u16 offset, u16 size)
{ assert(offset + size / 2 <= ARRAY_COUNT(outputColors)); CpuSet(src, outputColors + offset, size / 2); }
#include "menu_helpers.inc"

enum { WINDOW_BG, KANTO_MAPSEC_START = 1000, KANTO_MAPSEC_END = 1100, KANTO_MAPSEC_COUNT = 101,
       WEATHER_UNDERWATER_BUBBLES = 7 };
static struct {u16 regionMapSectionId; u8 weather;} gMapHeader;
static const u8 sMapSectionToThemeId[6] = {0,1,2,3,4,5};
static const u16 sMapPopUp_PaletteTable[6][16], sMapPopUp_Palette_Underwater[16];
static const u8 sMapPopUp_Table[6][960];
static u32 frameTiles;
static unsigned frameCalls, backdropCalls, tilemapCalls;
static u8 GetMapNamePopUpWindowId(void) { return 2; }
static u32 GetWindowAttribute(u8 id, u8 attr) { assert(id == 2 && attr == WINDOW_BG); return 0; }
static u16 LoadBgTiles(u8 bg, const void *src, u16 size, u16 offset)
{
    assert(bg == 0 && offset == 0x21D);
    s16 slot = RequestDma3Copy(src, tileVram + offset * 32, size, 1);
    assert(slot >= 0 && sDma3Requests[slot].mode == DMA_REQUEST_COPY32);
    /* Retire the actual queued copy exactly as the portable DMA engine does,
     * without an additional asset resolver that could hide a bad source. */
    memcpy(sDma3Requests[slot].dest, sDma3Requests[slot].src, sDma3Requests[slot].size);
    sDma3Requests[slot].size = 0;
    return slot;
}
static void FillBgTilemapBufferRect(u8 bg, u16 tile, u8 x, u8 y, u8 width, u8 height, u8 palette)
{
    assert(bg == 0 && tile >= 0x21D && tile <= 0x23A && x <= 12 && y <= 5);
    assert(width == 1 && height == 1 && palette == 14);
    assert(!(frameTiles & (1u << (tile - 0x21D))));
    frameTiles |= 1u << (tile - 0x21D); frameCalls++;
}
static void CallWindowFunction(u8 id, void (*callback)(u8,u8,u8,u8,u8,u8))
{ assert(id == 2); callback(0,1,1,10,3,0); }
static void PutWindowTilemap(u8 id) { assert(id == 2); tilemapCalls++; }
static void BlitBitmapToWindow(u8 id, const u8 *src, u16 x, u16 y, u16 width, u16 height)
{
    assert(id == 2 && src == sMapPopUp_Table[gMapHeader.regionMapSectionId]);
    assert(!x && !y && width == 80 && height == 24); backdropCalls++;
}
#include "popup_helpers.inc"

static void conditionTest(void)
{
    struct {u32 before; struct SpriteSheet entries[4]; u32 after;} sheets = {.before=0x11112222,.after=0x33334444};
    struct {u32 before; struct SpritePalette entries[3]; u32 after;} pals = {.before=0x55556666,.after=0x77778888};
    struct {u32 before; struct SpriteTemplate value; u32 after;} template = {.before=0x9999aaaa,.after=0xbbbbcccc};
    LoadConditionSelectionIcons(sheets.entries, &template.value, pals.entries);
    assert(sheets.before==0x11112222 && sheets.after==0x33334444);
    assert(pals.before==0x55556666 && pals.after==0x77778888);
    assert(template.before==0x9999aaaa && template.after==0xbbbbcccc);
    assert(sheets.entries[0].data==sConditionPokeball_Gfx && sheets.entries[0].size==0x100 && sheets.entries[0].tag==TAG_CONDITION_BALL);
    assert(sheets.entries[1].data==sConditionPokeballPlaceholder_Gfx && sheets.entries[1].size==0x20 && sheets.entries[1].tag==TAG_CONDITION_BALL_PLACEHOLDER);
    assert(sheets.entries[2].data==gPokenavConditionCancel_Gfx && sheets.entries[2].size==0x100 && sheets.entries[2].tag==TAG_CONDITION_CANCEL);
    assert(!sheets.entries[3].data && !sheets.entries[3].size && !sheets.entries[3].tag);
    assert(pals.entries[0].tag==TAG_CONDITION_BALL && pals.entries[1].tag==TAG_CONDITION_CANCEL);
    assert(!pals.entries[2].data && !pals.entries[2].tag);
    assert(template.value.tileTag==TAG_CONDITION_BALL && template.value.paletteTag==TAG_CONDITION_BALL);
    assert(template.value.oam==&sOam_ConditionSelectionIcon && template.value.anims==sAnims_ConditionSelectionIcon);
    assert(!template.value.images && template.value.affineAnims==gDummySpriteAffineAnimTable && template.value.callback==SpriteCallbackDummy);
    memset(outputColors, 0xa5, sizeof(outputColors));
    u16 expected[512]; memcpy(expected, outputColors, sizeof(expected));
    memcpy(expected + OBJ_PLTT_OFFSET, selectionColors, sizeof(selectionColors));
    for (unsigned i=0; i<2; i++) DoLoadSpritePalette(pals.entries[i].data, i*16);
    assert(!memcmp(expected, outputColors, sizeof(expected)));
    puts("PASS actual condition icon descriptors: both16-color banks, sentinel entries, template and destination guards");
}

static void popupTest(int selectedTheme)
{
    for (unsigned theme=0; theme<6; theme++) {
        if (selectedTheme >= 0 && theme != (unsigned)selectedTheme) continue;
        for (unsigned underwater=0; underwater<2; underwater++) {
            memset(tileVram, 0xa5, sizeof(tileVram));
            u8 expected[sizeof(tileVram)]; memcpy(expected,tileVram,sizeof(expected));
            memcpy(expected+0x21D*32,outlines[theme],960);
            gMapHeader.regionMapSectionId=theme;
            gMapHeader.weather=underwater?WEATHER_UNDERWATER_BUBBLES:0;
            frameTiles=frameCalls=backdropCalls=tilemapCalls=0;
            LoadMapNamePopUpWindowBg();
            assert(!memcmp(expected,tileVram,sizeof(expected)));
            assert(frameCalls==30 && frameTiles==0x3fffffff && backdropCalls==1 && tilemapCalls==1);
        }
    }
    puts("PASS actual popup loader/DMA request: six960-byte outline rows,30 frame tiles, both palette paths and VRAM guards");
}

int main(int argc, char **argv)
{
    assert(argc==3);
    for(unsigned i=0;i<32;i++)selectionColors[i]=0x100+i*41;
    for(unsigned row=0;row<6;row++)for(unsigned i=0;i<960;i++)outlines[row][i]=(i*13+row*7)%251;
    if(!strcmp(argv[1],"all")||!strcmp(argv[1],"condition"))conditionTest();
    if(!strcmp(argv[1],"all")||!strcmp(argv[1],"popup"))popupTest(atoi(argv[2]));
    return 0;
}
