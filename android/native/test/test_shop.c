#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gba/defines.h"

typedef uint8_t u8, bool8;
typedef uint16_t u16;
typedef int16_t s16;
#define NUM_METATILES_IN_PRIMARY 512
#define NUM_TILES_PER_METATILE 8
enum { METATILE_LAYER_TYPE_NORMAL, METATILE_LAYER_TYPE_COVERED, METATILE_LAYER_TYPE_SPLIT };
struct Tileset { const u16 *metatiles; };
struct MapLayout { const struct Tileset *primaryTileset, *secondaryTileset; };
static struct { const struct MapLayout *mapLayout; } gMapHeader;
static struct { u16 tilemapBuffers[4][0x400]; } shop, *sShopData = &shop;
static u16 primary[512 * 8], secondary[512 * 8];
#ifdef PORT_BRIDGE
static const u16 primaryStub[1], secondaryStub[1];
static unsigned resolutions;
static const void *Port_ResolveAssetPointer(const void *base) __attribute__((unused));
static const void *Port_ResolveAssetPointer(const void *base)
{
    resolutions++;
    // Offsetting the two-byte stub first cannot be repaired by resolution.
    assert(base == primaryStub || base == secondaryStub);
    return base == primaryStub ? primary : secondary;
}
#endif
static void GetXYCoordsOneStepInFrontOfPlayer(s16 *x, s16 *y) { *x = 4; *y = 4; }
static u16 MapGridGetMetatileIdAt(s16 x, s16 y)
{
    assert(x >= 0 && x < 15 && y >= 0 && y < 10);
    return (x + y * 15) * 37 % 1024;
}
static u8 MapGridGetMetatileLayerTypeAt(s16 x, s16 y) { return (x + y) % 3; }
#include "shop_helpers.inc"

static void assets(void)
{
    for (unsigned i = 0; i < 512 * 8; i++) { primary[i] = 0x1000 + i; secondary[i] = 0x6000 + i; }
#ifdef PORT_BRIDGE
    const struct Tileset sets[2] = {{primaryStub}, {secondaryStub}};
#else
    const struct Tileset sets[2] = {{primary}, {secondary}};
#endif
    const struct MapLayout layout = {sets, sets + 1};
    gMapHeader.mapLayout = &layout;
    memset(&shop, 0, sizeof(shop));
    // Some shop foreground cells cover the map; exercise that real decision.
    for (unsigned y = 0; y < 10; y++) for (unsigned x = 0; x < 15; x++)
        if ((x + y) % 4 == 0) shop.tilemapBuffers[0][y * 64 + x * 2] = 1;
    u16 expected[4][0x400]; memcpy(expected, shop.tilemapBuffers, sizeof(expected));
    for (unsigned y = 0; y < 10; y++) for (unsigned x = 0; x < 15; x++) {
        unsigned id = MapGridGetMetatileIdAt(x, y);
        const u16 *tiles = id < 512 ? primary + id * 8 : secondary + (id - 512) * 8;
        unsigned type = (x + y) % 4 == 0 ? METATILE_LAYER_TYPE_COVERED : (x + y) % 3;
        unsigned lower = type == METATILE_LAYER_TYPE_NORMAL ? 3 : 2;
        unsigned upper = type == METATILE_LAYER_TYPE_COVERED ? 3 : 1;
        unsigned offset = y * 64 + x * 2;
        const unsigned quadrants[] = {0, 1, 32, 33};
        for (unsigned q = 0; q < 4; q++) {
            expected[lower][offset + quadrants[q]] = tiles[q];
            expected[upper][offset + quadrants[q]] = tiles[q + 4];
        }
    }
    BuyMenuDrawMapBg();
    assert(!memcmp(expected, shop.tilemapBuffers, sizeof(expected)));
#ifdef PORT_BRIDGE
    assert(resolutions == 2);
#endif
    puts("PASS shop assets: all150 primary/secondary metatiles, three layer modes, covered cells and unchanged unused cells");
}

#define NAME_(x) #x
#define NAME(x) NAME_(x)
static void geometry(void)
{
    fprintf(stderr, "shop target geometry %dx%d, VBlank setter %s\n", DISPLAY_WIDTH, DISPLAY_HEIGHT, NAME(SetVBlankCallback));
    assert(DISPLAY_WIDTH == 240 && DISPLAY_HEIGHT == 160);
    assert(!strcmp(NAME(SetVBlankCallback), "CtrCentred_SetVBlankCallback"));
    puts("PASS shop target: real make flags provide GBA geometry and only the owned Buy VBlank is centred");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "geometry")) assets();
    if (strcmp(argv[1], "assets")) geometry();
    return 0;
}
