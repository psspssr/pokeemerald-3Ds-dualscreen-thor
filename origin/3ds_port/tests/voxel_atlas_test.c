/* The real atlas composer against the per-texel reference it replaced, on
 * random tiles, palettes, flips and layers: every texel, and the black-filler
 * verdict, must come out the same. */
#include <assert.h>
#include <stdio.h>
#include "../src/voxel/voxel_atlas.c"

#include "../src/3ds_video_decode.c"

/*
 * voxel_atlas.c's world and asset dependencies: a fake tileset pair for the
 * whole-atlas tests below, whose tiles, metatiles, palettes and referenced
 * ids are the arrays further down.
 */
unsigned char g_PortLogActive;
void Port_Log_Printf(const char *fmt, ...) { (void)fmt; }
const void *Port_ResolveAssetPointer(const void *p) { return p; }
static const int kPrimary = 1, kSecondary = 2;
static uint8_t sPrimaryTiles[512 * 32], sSecondaryTiles[512 * 32];
static uint16_t sPrimaryPalettes[16 * 16], sSecondaryPalettes[16 * 16];
static uint16_t sMetatiles[1024 * 8];
static uint8_t sUsed[1024];
void VoxelWorld_MarkUsedMetatiles(const void *p, const void *s, uint8_t *used)
{
    if (p == &kPrimary && s == &kSecondary)
        memcpy(used, sUsed, sizeof(sUsed));
}
bool Voxel_LoadTiles(const void *t, uint8_t *d, uint32_t n)
{
    if (t != &kPrimary && t != &kSecondary)
        return false;
    memcpy(d, t == &kPrimary ? sPrimaryTiles : sSecondaryTiles, n < 512 * 32 ? n : 512 * 32);
    return true;
}
const uint16_t *Voxel_ResolveMetatiles(const void *t)
{
    return t == &kPrimary ? sMetatiles : t == &kSecondary ? sMetatiles + 512 * 8 : NULL;
}
const uint16_t *Voxel_ResolvePalettes(const void *t)
{
    return t == &kPrimary ? sPrimaryPalettes : t == &kSecondary ? sSecondaryPalettes : NULL;
}
unsigned Voxel_MetatileCount(const void *t, unsigned limit) { (void)t; return limit; }

/* ── The composer as it was, texel by texel ─────────────────────────────── */

static void ReferenceCompose(const struct AtlasSource *src, const uint16_t *entries,
                             uint16_t *dest, unsigned slot)
{
    unsigned baseX = (slot % VOXEL_ATLAS_COLUMNS) * VOXEL_ATLAS_SLOT;
    unsigned baseY = (slot / VOXEL_ATLAS_COLUMNS) * VOXEL_ATLAS_SLOT;

    for (unsigned layer = 0; layer < 2; ++layer)
        for (unsigned quadrant = 0; quadrant < 4; ++quadrant)
        {
            unsigned dstX = (quadrant % 2) * SUBTILE_PX, dstY = (quadrant / 2) * SUBTILE_PX;
            uint16_t entry = entries[layer * 4 + quadrant];
            unsigned tileId = entry & 0x3FF, paletteId = (entry >> 12) & 0xF;
            bool flipX = (entry >> 10) & 1, flipY = (entry >> 11) & 1;
            const uint8_t *tiles = tileId < TILES_PER_TILESET ? src->primaryTiles : src->secondaryTiles;
            unsigned localTile = tileId < TILES_PER_TILESET ? tileId : tileId - TILES_PER_TILESET;

            if (tiles == NULL)
                continue;
            for (unsigned py = 0; py < SUBTILE_PX; ++py)
                for (unsigned px = 0; px < SUBTILE_PX; ++px)
                {
                    unsigned srcX = flipX ? (SUBTILE_PX - 1 - px) : px;
                    unsigned srcY = flipY ? (SUBTILE_PX - 1 - py) : py;
                    uint8_t packed = tiles[localTile * 32 + srcY * 4 + srcX / 2];
                    unsigned colorIdx = (srcX & 1) ? (packed >> 4) : (packed & 0xF);
                    uint16_t bgr15;

                    if (layer != 0 && colorIdx == 0)
                        continue;
                    if (layer == 0 && colorIdx == 0)
                        bgr15 = src->primaryPalettes != NULL ? src->primaryPalettes[0] : 0;
                    else
                        bgr15 = LookupColor(src, paletteId, colorIdx);
                    dest[CtrVideo_Texel(baseX + dstX + px, baseY + dstY + py, VOXEL_ATLAS_W)] =
                        CtrVideo_RGBA5551(bgr15);
                }
        }
}

static bool ReferenceBlack(const struct AtlasSource *src, const uint16_t *entries)
{
    for (unsigned layer = 0; layer < 2; ++layer)
        for (unsigned quadrant = 0; quadrant < 4; ++quadrant)
        {
            uint16_t entry = entries[layer * 4 + quadrant];
            unsigned tileId = entry & 0x3FF, paletteId = (entry >> 12) & 0xF;
            const uint8_t *tiles = tileId < TILES_PER_TILESET ? src->primaryTiles : src->secondaryTiles;
            unsigned local = tileId < TILES_PER_TILESET ? tileId : tileId - TILES_PER_TILESET;

            if (tiles == NULL)
                return false;
            for (unsigned b = 0; b < 32; ++b)
                for (unsigned half = 0; half < 2; ++half)
                {
                    unsigned colorIdx = half ? (tiles[local * 32 + b] >> 4) : (tiles[local * 32 + b] & 0xF);
                    uint16_t bgr15;

                    if (colorIdx == 0)
                    {
                        if (layer != 0)
                            continue;
                        bgr15 = src->primaryPalettes != NULL ? src->primaryPalettes[0] : 0;
                    }
                    else
                        bgr15 = LookupColor(src, paletteId, colorIdx);
                    if ((bgr15 & 0x7FFF) != 0)
                        return false;
                }
        }
    return true;
}

static uint32_t sSeed = 12345;
static unsigned Random(void)
{
    sSeed = sSeed * 1103515245u + 12345u;
    return sSeed >> 8;
}

static uint16_t sNew[VOXEL_ATLAS_PIXELS], sOld[VOXEL_ATLAS_PIXELS];

static void Fill(unsigned blackEvery)
{
    for (unsigned i = 0; i < sizeof(sPrimaryTiles); ++i)
    {
        sPrimaryTiles[i] = (uint8_t)Random();
        sSecondaryTiles[i] = (uint8_t)Random();
    }
    /* A few all-zero tiles, which only draw the backdrop and layer 1 keys out. */
    memset(sPrimaryTiles + 7 * 32, 0, 32);
    memset(sSecondaryTiles + 9 * 32, 0, 32);
    for (unsigned i = 0; i < 16 * 16; ++i)
    {
        sPrimaryPalettes[i] = (uint16_t)(Random() & 0xFFFF);
        sSecondaryPalettes[i] = (uint16_t)(Random() & 0xFFFF);
        if (blackEvery && i % blackEvery != 1)
        {
            sPrimaryPalettes[i] &= 0x8000;
            sSecondaryPalettes[i] &= 0x8000;
        }
    }
}

static void Run(struct AtlasSource *src, unsigned trials, unsigned *blacks)
{
    BuildColorTable(src);
    InitMorton();
    for (unsigned t = 0; t < trials; ++t)
    {
        uint16_t entries[8];
        unsigned slot = Random() % VOXEL_SOLID_BASE;
        bool black;

        for (unsigned e = 0; e < 8; ++e)
            entries[e] = (uint16_t)(Random() & 0xFFFF);
        if (t % 5 == 0)
            entries[Random() % 8] = (uint16_t)((Random() & 0xF000) | 7); /* blank tile */
        memset(sNew, 0x5A, sizeof(sNew));
        memset(sOld, 0x5A, sizeof(sOld));
        black = ComposeMetatile(src, entries, sNew, slot);
        ReferenceCompose(src, entries, sOld, slot);
        assert(memcmp(sNew, sOld, sizeof(sNew)) == 0);
        assert(black == ReferenceBlack(src, entries));
        *blacks += black;
    }
}

/* ── Whole atlases: composed a slice at a time, exactly as in one go ────── */

static uint16_t sOneGo[VOXEL_ATLAS_PIXELS], sSliced[VOXEL_ATLAS_PIXELS];

static void Sliced(const VoxelMapInstance *inst, const VoxelAtlasMap *base, VoxelAtlasMap *out,
                   unsigned *added)
{
    static VoxelAtlasJob job;
    unsigned steps = 0;

    memset(sSliced, 0x5A, sizeof(sSliced));
    assert(VoxelAtlas_JobBegin(&job, inst, sSliced, base));
    while (!VoxelAtlas_JobStep(&job, 1 + Random() % 9))
        ++steps;
    assert(steps > 20 && !job.active && job.ok);
    *out = job.map;
    *added = job.added;
}

static void WholeAtlases(void)
{
    VoxelMapInstance inst;
    VoxelAtlasMap oneGo, sliced, base;
    unsigned added;

    memset(&inst, 0, sizeof(inst));
    inst.primaryTileset = &kPrimary;
    inst.secondaryTileset = &kSecondary;
    Fill(0);
    for (unsigned i = 0; i < sizeof(sMetatiles) / sizeof(sMetatiles[0]); ++i)
        sMetatiles[i] = (uint16_t)(Random() & 0xFFFF);

    /* A fresh atlas of a map that references about a third of the ids. */
    for (unsigned m = 0; m < 1024; ++m)
        sUsed[m] = Random() % 3 == 0;
    memset(sOneGo, 0x5A, sizeof(sOneGo));
    assert(VoxelAtlas_Build(&inst, sOneGo, &oneGo, false));
    Sliced(&inst, NULL, &sliced, &added);
    assert(memcmp(sOneGo, sSliced, sizeof(sOneGo)) == 0);
    assert(memcmp(&oneGo, &sliced, sizeof(oneGo)) == 0);
    assert(added == oneGo.used && !oneGo.overflowed);

    /* The same atlas extended with more ids: nothing it had moves. */
    base = oneGo;
    for (unsigned m = 0; m < 1024; ++m)
        sUsed[m] = sUsed[m] || Random() % 4 == 0;
    assert(VoxelAtlas_Build(&inst, sOneGo, &oneGo, true));
    Sliced(&inst, &base, &sliced, &added);
    assert(memcmp(sOneGo, sSliced, sizeof(sOneGo)) == 0);
    assert(memcmp(&oneGo, &sliced, sizeof(oneGo)) == 0);
    assert(added == oneGo.used - base.used && added > 0);
    for (unsigned m = 0; m < 1024; ++m)
        assert(base.slotOf[m] == 0 || oneGo.slotOf[m] == base.slotOf[m]);

    /* Every id: the atlas fills up, says so, and marks the rest as seen. */
    base = oneGo;
    memset(sUsed, 1, sizeof(sUsed));
    Sliced(&inst, &base, &sliced, &added);
    assert(sliced.overflowed && sliced.used == VOXEL_SOLID_BASE);
    for (unsigned m = 0; m < 1024; ++m)
        assert(sliced.slotOf[m] != 0);
    /* And extended again with nothing new, it adds nothing. */
    base = sliced;
    Sliced(&inst, &base, &sliced, &added);
    assert(added == 0 && memcmp(&base, &sliced, sizeof(base)) == 0);
}

static void AnimatedTiles(void)
{
    VoxelMapInstance inst = {0};
    VoxelAtlasMap map;
    uint8_t live[1024 * 32], dirty[128] = {0};
    struct AtlasSource src = {0};
    unsigned slot1, slot2;

    inst.primaryTileset = &kPrimary;
    inst.secondaryTileset = &kSecondary;
    memset(sUsed, 0, sizeof(sUsed));
    memset(sMetatiles, 0, sizeof(sMetatiles));
    sUsed[1] = sUsed[2] = 1;
    for (unsigned t = 0; t < 4; ++t)
    {
        sMetatiles[1 * 8 + t] = 432;
        sMetatiles[2 * 8 + t] = 10;
    }
    Fill(0);
    assert(VoxelAtlas_Build(&inst, sOneGo, &map, false));
    slot1 = map.slotOf[1] - 1;
    slot2 = map.slotOf[2] - 1;
    assert(slot1 != slot2);
    memcpy(sSliced, sOneGo, sizeof(sOneGo));
    memcpy(live, sPrimaryTiles, sizeof(sPrimaryTiles));
    memcpy(live + sizeof(sPrimaryTiles), sSecondaryTiles, sizeof(sSecondaryTiles));
    memset(live + 432 * 32, 0x11, 32);
    dirty[432 >> 3] = 1u << (432 & 7);
    assert(VoxelAtlas_RefreshAnimated(&inst, &map, sOneGo, live, dirty) == 1);
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x)
        {
            unsigned offset = CtrVideo_Texel((slot2 % VOXEL_ATLAS_COLUMNS) * 16 + x,
                                               (slot2 / VOXEL_ATLAS_COLUMNS) * 16 + y,
                                               VOXEL_ATLAS_W);
            assert(sOneGo[offset] == sSliced[offset]);
        }
    src.primaryTiles = live;
    src.secondaryTiles = live + sizeof(sPrimaryTiles);
    src.primaryPalettes = sPrimaryPalettes;
    src.secondaryPalettes = sSecondaryPalettes;
    src.primaryMetatiles = sMetatiles;
    src.secondaryMetatiles = sMetatiles + 512 * 8;
    memset(sOld, 0, sizeof(sOld));
    ReferenceCompose(&src, sMetatiles + 8, sOld, slot1);
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x)
        {
            unsigned offset = CtrVideo_Texel((slot1 % VOXEL_ATLAS_COLUMNS) * 16 + x,
                                               (slot1 / VOXEL_ATLAS_COLUMNS) * 16 + y,
                                               VOXEL_ATLAS_W);
            assert(sOneGo[offset] == sOld[offset]);
        }
    memset(dirty, 0, sizeof(dirty));
    assert(VoxelAtlas_RefreshAnimated(&inst, &map, sOneGo, live, dirty) == 0);
}

int main(void)
{
    struct AtlasSource src;
    unsigned blacks = 0;

    memset(&src, 0, sizeof(src));
    src.primaryTiles = sPrimaryTiles;
    src.secondaryTiles = sSecondaryTiles;
    src.primaryPalettes = sPrimaryPalettes;
    src.secondaryPalettes = sSecondaryPalettes;

    Fill(0);
    Run(&src, 2000, &blacks);
    /* Mostly black, then all black: the verdict is exercised both ways. */
    Fill(16);
    Run(&src, 2000, &blacks);
    Fill(1);
    Run(&src, 2000, &blacks);
    assert(blacks != 0);
    /* Missing secondary tiles and palettes. */
    src.secondaryTiles = NULL;
    src.secondaryPalettes = NULL;
    Run(&src, 2000, &blacks);
    src.primaryPalettes = NULL;
    Run(&src, 1000, &blacks);
    WholeAtlases();
    AnimatedTiles();
    printf("PASS voxel atlas: block composer equals per-texel reference (%u black), "
           "sliced atlases equal one-go ones, fresh and extended; selective animation\n", blacks);
    return 0;
}
