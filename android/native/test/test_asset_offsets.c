#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gba/defines.h"
#include "gba/io_reg.h"
typedef uint8_t u8, bool8;
typedef int16_t s16;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
#define ARRAY_COUNT(a) (sizeof(a) / sizeof(*(a)))
#define BG_PLTT_ID(n) ((n) * 16)
#define FALSE 0
static u16 evolutionColors[16], reels[220];
static u32 shrink[120];
static u8 spotlight[2880], orderNumbers[128], randomOrder[32];
#ifdef PORT_BRIDGE
static const u16 sBgAnim_Pal[1], sReelTimeWindow_Tilemap[1];
static const u32 sShrinkingBoxTileset[1];
static const u8 sSpotlight_Gfx[1], gContestNextTurnNumbersGfx[1], gContestNextTurnRandomGfx[1];
static const void *Port_ResolveAssetPointer(const void *ptr)
{
    if (ptr == sBgAnim_Pal) return evolutionColors;
    if (ptr == sReelTimeWindow_Tilemap) return reels;
    if (ptr == sShrinkingBoxTileset) return shrink;
    if (ptr == sSpotlight_Gfx) return spotlight;
    if (ptr == gContestNextTurnNumbersGfx) return orderNumbers;
    if (ptr == gContestNextTurnRandomGfx) return randomOrder;
    return ptr;
}
static bool Port_LoadAssetPointerToBufferSized(const void *ptr, void *dest, u32 bytes)
{
    const void *resolved = Port_ResolveAssetPointer(ptr);
    if (resolved == ptr) return false;
    memcpy(dest, resolved, bytes);
    return true;
}
static bool Port_LoadAssetPointerToBuffer(const void *ptr, void *dest, u32 bytes)
{ return Port_LoadAssetPointerToBufferSized(ptr, dest, bytes); }
#else
#define sBgAnim_Pal evolutionColors
#define sReelTimeWindow_Tilemap reels
#define sShrinkingBoxTileset shrink
#define sSpotlight_Gfx spotlight
#define gContestNextTurnNumbersGfx orderNumbers
#define gContestNextTurnRandomGfx randomOrder
#endif
#include "asset_bios.inc"
static void CpuCopy16(const void *src, void *dst, u32 size) { CpuSet(src, dst, size / 2); }
static void CpuFill16(u16 value, void *dst, u32 size) { CpuSet(&value, dst, (1u << 24) | (size / 2)); }
static u16 vram[0x10000 / 2], transitionMap[1024], transitionTiles[1024], reelMap[1024];
#undef BG_CHAR_ADDR
#undef BG_SCREEN_ADDR
#define BG_CHAR_ADDR(n) ((uintptr_t)vram + (n) * 0x4000)
#define BG_SCREEN_ADDR(n) ((uintptr_t)vram + (n) * 0x800)
static struct Sprite { struct {u8 priority, paletteNum;} oam; s16 data[8], y; } gSprites[1];
enum { FLDEFFOBJ_RAYQUAZA, SPIRAL_INWARD_START };
static const void *gFieldEffectObjectTemplatePointers[1];
static u8 CreateSprite(const void *template, s16 x, s16 y, u8 priority)
{ assert(template == gFieldEffectObjectTemplatePointers[0] && x == 120 && y == -24 && priority == 1); gSprites[0].y = y; return 0; }
static void SetGpuReg(u16 reg, u16 value) { (void)reg; (void)value; }
static const u16 sSpotlight_Pal[16], sFieldEffectPal_Pokeball[16];
static void LoadPalette(const void *src, u16 offset, u16 bytes)
{ assert(src && (offset == 12 * 16 || offset == 15 * 16) && bytes == 32); }
struct Task { s16 data[16]; };
#define tState data[0]
#define tDelay data[1]
#define tShrinkStage data[2]
static struct { s16 state, position, moveIndex, reboundPosition; bool outward; } sRectangularSpiralLines[4];
static void GetBg0TilesDst(u16 **map, u16 **tiles) { *map = transitionMap; *tiles = transitionTiles; }
static void GetBg0TilemapDst(u16 **tiles) { *tiles = transitionTiles; }
static void LoadBgTilemap(u8 bg, const void *src, u16 bytes, u16 offset)
{ assert(bg == 1 && bytes == 2 && offset < ARRAY_COUNT(reelMap)); CpuCopy16(src, reelMap + offset, bytes); }
static struct { u8 turnOrderMod, nextTurnOrder; } eContestantStatus[4];
#include "asset_helpers.inc"

static void evolutionTest(void)
{
    u16 actual[ARRAY_COUNT(sBgAnim_PalIndexes) * 16 + 2];
    memset(actual, 0x55, sizeof(actual));
    InitMovingBgPalette(actual + 1);
    assert(actual[0] == 0x5555 && actual[ARRAY_COUNT(actual) - 1] == 0x5555);
    for (unsigned i = 0; i < ARRAY_COUNT(sBgAnim_PalIndexes); i++)
        for (unsigned j = 0; j < 16; j++)
            assert(actual[1 + i * 16 + j] == evolutionColors[sBgAnim_PalIndexes[i][j]]);
    puts("PASS actual evolution animation palette: all phases, all16 colors, destination guards");
}
static void spotlightTest(void)
{
    memset(vram, 0xa5, sizeof(vram));
    u16 expected[ARRAY_COUNT(vram)]; memcpy(expected, vram, sizeof(vram));
    memcpy((u8 *)expected + 0x8000 + 32, spotlight, sizeof(spotlight));
    for (unsigned i = 3; i < 15; i++) for (unsigned j = 12; j < 18; j++)
        expected[0xF800 / 2 + i * 32 + j] = 0xBFF4 + i * 6 + j + 1;
    assert(FldEff_RayquazaSpotlight() == 0);
    assert(!memcmp(vram, expected, sizeof(vram)));
    assert(gSprites[0].oam.priority == 1 && gSprites[0].oam.paletteNum == 4 && gSprites[0].data[4] == -24);
    puts("PASS actual Rayquaza spotlight: all90 tiles and full VRAM guard comparison");
}
static void spiralTest(void)
{
    struct Task task = {0};
    memset(transitionTiles, 0xa5, sizeof(transitionTiles));
    u16 expected[ARRAY_COUNT(transitionTiles)]; memcpy(expected, transitionTiles, sizeof(expected));
    memcpy(expected, shrink, 32); memcpy(expected + 32, shrink + 0x70, 32);
    assert(!RectangularSpiral_Init(&task));
    assert(!memcmp(expected, transitionTiles, sizeof(expected)));
    assert(task.tState == 1 && task.data[3] == 1);
    for (unsigned i = 0; i < ARRAY_COUNT(transitionMap); i++) assert(transitionMap[i] == 0xf000);
    puts("PASS actual spiral transition: both tiles, map and unchanged surrounding tiles");
}
static void gridTest(void)
{
    struct Task task = {0};
    for (unsigned stage = 1; stage <= 14; stage++) {
        task.tDelay = 0;
        memset(transitionTiles, 0xa5, sizeof(transitionTiles));
        u16 expected[ARRAY_COUNT(transitionTiles)]; memcpy(expected, transitionTiles, sizeof(expected));
        memcpy(expected, shrink + stage * 8, 32);
        assert(!GridSquares_Main(&task));
        assert(!memcmp(expected, transitionTiles, sizeof(expected)) && task.tShrinkStage == (int)stage);
        assert(task.tDelay == (stage == 14 ? 15 : 2));
    }
    assert(task.tState == 1);
    puts("PASS actual grid transition:14 stages, original delay/end state, tile guards");
}
static void reelsTest(void)
{
    for (unsigned column = 0; column < 22; column++) {
        unsigned sourceColumn = column < 20 ? column : column == 20 ? 0 : 19;
        unsigned destinationColumn = column < 20 ? column : column == 20 ? 30 : 0;
        memset(reelMap, 0xa5, sizeof(reelMap));
        u16 expected[ARRAY_COUNT(reelMap)]; memcpy(expected, reelMap, sizeof(expected));
        for (unsigned row = 4; row < 15; row++)
            expected[row * 32 + destinationColumn] = reels[(row - 4) * 20 + sourceColumn];
        LoadReelTimeWindowTilemap(destinationColumn, sourceColumn);
        assert(!memcmp(expected, reelMap, sizeof(expected)));
    }
    puts("PASS actual Reel Time window: all220 cells, independent source/destination columns and guards");
}
static void contestTest(void)
{
    u8 output[32];
    for (unsigned mon = 0; mon < 4; mon++) for (unsigned order = 0; order < 4; order++) {
        eContestantStatus[mon].turnOrderMod = 1; eContestantStatus[mon].nextTurnOrder = order;
        CpuSet(GetTurnOrderNumberGfx(mon), output, 16);
        assert(!memcmp(output, orderNumbers + order * 32, 32));
        eContestantStatus[mon].turnOrderMod = 2;
        CpuSet(GetTurnOrderNumberGfx(mon), output, 16);
        assert(!memcmp(output, randomOrder, 32));
    }
    puts("PASS actual Contest order helper/CpuSet:4 positions for all contestants, random fallback");
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    for (unsigned i = 0; i < ARRAY_COUNT(evolutionColors); i++) evolutionColors[i] = 0x200 + i * 79;
    for (unsigned i = 0; i < ARRAY_COUNT(reels); i++) reels[i] = 0x800 + i;
    for (unsigned i = 0; i < ARRAY_COUNT(shrink); i++) shrink[i] = 0x11111111 * (i % 15 + 1);
    for (unsigned i = 0; i < sizeof(spotlight); i++) spotlight[i] = (i * 13 + 1) % 251;
    for (unsigned i = 0; i < sizeof(orderNumbers); i++) orderNumbers[i] = (i / 32 + 1) * 7;
    memset(randomOrder, 0x7a, sizeof(randomOrder));
#define RUN(name) if (!strcmp(argv[1], "all") || !strcmp(argv[1], #name)) name##Test()
    RUN(evolution); RUN(spotlight); RUN(spiral); RUN(grid); RUN(reels); RUN(contest);
    return 0;
}
