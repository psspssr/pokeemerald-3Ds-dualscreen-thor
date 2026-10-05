#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants/species.h"

typedef uint8_t u8, bool8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t s16;
typedef int32_t s32;
#define ARRAY_COUNT(a) (sizeof(a) / sizeof(*(a)))
#define SPINDA_SPOT_WIDTH 16
#define SPINDA_SPOT_HEIGHT 16
#define TILE_SIZE_4BPP 32
#define SWAP(a,b,temp) do { (temp) = (a); (a) = (b); (b) = (temp); } while (0)

struct SpriteFrameImage { const u8 *data; u16 size; };
struct SpriteTemplate { const struct SpriteFrameImage *images; };
struct Sprite {
    s16 x, y, centerToCornerVecX, data[8];
    void (*callback)(struct Sprite *);
};
static struct { s16 data[16]; void (*func)(u8); } gTasks[2];
static struct Sprite gSprites[1];
static const struct SpriteTemplate sSpriteTemplate_FallingFossil = {0};
static u16 sDebug_DisintegrationData[8];
#define tState data[0]
#include "asset_pair_data.inc"

static unsigned allocated, freed, resumed, destroyed, randomCalls, animations;
static unsigned fossilResolved, spotsResolved;
static unsigned destroyedTasks, towerUploadSize;
static u8 towerUpload[2336];
static const u16 sMirageTowerTilemap[1];
#define BG_SCREEN_SIZE 2048
#define BG_ATTR_PRIORITY 0
#define BG_COORD_SET 0
#define TASK_NONE 255
#define REG_OFFSET_BG2CNT 2
#define REG_OFFSET_BG0CNT 0
#define BGCNT_PRIORITY(x) (x)
static void *AllocZeroed(size_t size)
{
    allocated++;
    void *out = calloc(1, size);
    assert(out);
    return out;
}
static void *Alloc(size_t size) { return AllocZeroed(size); }
static void Free(void *data) { freed++; free(data); }
#define FREE_AND_SET_NULL(p) do { Free(p); (p) = NULL; } while (0)
static void SpriteCallbackDummy(struct Sprite *sprite) { (void)sprite; }
static void SpriteCB_FallingFossil(struct Sprite *sprite);
static void UpdateDisintegrationEffect(u8 *tiles, u16 randId, u8 c, u8 size, u8 offset);
static u8 CreateSprite(const struct SpriteTemplate *template, s16 x, s16 y, unsigned priority)
{
    assert(template->images == sFallingFossil->frameImage && priority == 1);
    assert(x == 128 && y == -16);
    gSprites[0].x = x; gSprites[0].y = y;
    gSprites[0].callback = SpriteCallbackDummy;
    return 0;
}
static void DestroySprite(struct Sprite *sprite) { assert(sprite == gSprites); destroyed++; }
static void ScriptContext_Enable(void) { resumed++; }
static void StartSpriteAnim(struct Sprite *sprite, unsigned anim)
{ assert(sprite == gSprites && anim == 0); animations++; }
static void FreeAllWindowBuffers(void) { }
static void SetBgAttribute(unsigned bg, unsigned attr, unsigned value)
{ assert(bg == 0 && attr == BG_ATTR_PRIORITY && (value == 0 || value == 2)); }
static void ChangeBgX(unsigned bg, unsigned value, unsigned op)
{ assert(bg == 0 && value == 0 && op == BG_COORD_SET); }
static void ChangeBgY(unsigned bg, unsigned value, unsigned op) { ChangeBgX(bg, value, op); }
static void LoadBgTiles(unsigned bg, const void *data, unsigned size, unsigned offset)
{
    assert(bg == 0 && offset == 0 && size <= sizeof(towerUpload));
    assert(data == sMirageTowerGfxBuffer);
    towerUploadSize = size;
    memcpy(towerUpload, data, size);
}
static void SetBgTilemapBuffer(unsigned bg, void *data)
{ assert(bg == 0 && data == sMirageTowerTilemapBuffer); }
static void CopyToBgTilemapBufferRect_ChangePalette(unsigned bg, const void *data,
                                                  unsigned x, unsigned y, unsigned w, unsigned h, unsigned palette)
{ assert(bg == 0 && data == sMirageTowerTilemap && x == 12 && y == 29 && w == 6 && h == 12 && palette == 17); }
static void CopyBgTilemapBufferToVram(unsigned bg) { assert(bg == 0); }
static void ShowBg(unsigned bg) { assert(bg == 0); }
static void SetInvisibleMirageTowerMetatiles(void) { }
static void UpdateBgShake(u8 task) { (void)task; }
static u8 CreateTask(void (*func)(u8), unsigned priority)
{ assert(func == UpdateBgShake && priority == 10); gTasks[1].func = func; return 1; }
static void DestroyTask(u8 task) { assert(task < 2); gTasks[task].func = NULL; destroyedTasks++; }
static u8 FindTaskIdByFunc(void (*func)(u8)) { return gTasks[1].func == func ? 1 : TASK_NONE; }
static void UnsetBgTilemapBuffer(unsigned bg) { assert(bg == 0); }
static void SetBgShakeOffsets(void) { assert(sBgShakeOffsets->bgHOFS == 0 && sBgShakeOffsets->bgVOFS == 0); }
static void SetGpuRegBits(unsigned reg, unsigned value)
{ assert((reg == REG_OFFSET_BG2CNT && value == 2) || (reg == REG_OFFSET_BG0CNT && value == 0)); }
static void InitStandardTextBoxWindows(void) { }
static u16 Random(void)
{
    static u32 seed = 17;
    randomCalls++;
    seed = seed * 1103515245u + 12345u;
    return seed >> 16;
}
#ifdef PORT_BRIDGE
static u32 Port_GetSpriteFrameSize(const void *base, u32 size) __attribute__((unused));
static u32 Port_GetSpriteFrameSize(const void *base, u32 size)
{
    if (base == sFossil_Gfx) {
        assert(size == sizeof(sFossil_Gfx));
        return size >= 32 ? size : sizeof(fossilPayload);
    }
    assert(base == sMirageTower_Gfx && size == sizeof(sMirageTower_Gfx));
    return size >= 32 ? size : sizeof(towerPayload);
}
static const void *Port_ResolveAssetPointer(const void *base) __attribute__((unused));
static const void *Port_ResolveAssetPointer(const void *base)
{
    if (base == sFossil_Gfx) {
        fossilResolved++;
        return TEST_EXTERNAL ? fossilPayload : base;
    }
    assert(base == gSpindaSpotGraphics); // Resolve the grouped base, not image[spot].
    spotsResolved++;
    return TEST_EXTERNAL ? spotImages : base;
}
static bool Port_LoadAssetPointerToBufferSized(const void *base, void *out, u32 size)
{
    assert(base == sMirageTower_Gfx && size <= sizeof(towerPayload));
    if (!TEST_EXTERNAL || !size) return false;
    memcpy(out, towerPayload, size);
    return true;
}
static bool Port_LoadAssetPointerToBuffer(const void *base, void *out, u32 size)
{ return Port_LoadAssetPointerToBufferSized(base, out, size); }
#endif
#include "asset_pair_helpers.inc"

static void tower(void)
{
    memset(gTasks, 0, sizeof(gTasks));
    allocated = freed = resumed = randomCalls = destroyedTasks = 0;
    for (unsigned state = 0; state <= 6; state++) InitMirageTowerShake(0);
    assert(sMirageTowerGfxBuffer && sMirageTowerTilemapBuffer && sBgShakeOffsets);
    fprintf(stderr, "tower declared=%zu upload=%u, actual image=%zu\n",
            sizeof(sMirageTower_Gfx), towerUploadSize, sizeof(towerPayload));
    if (towerUploadSize == sizeof(towerPayload))
        assert(!memcmp(sMirageTowerGfxBuffer, towerPayload, sizeof(towerPayload)));
    assert(resumed == 1 && destroyedTasks == 1);
    memset(gTasks[0].data, 0, sizeof(gTasks[0].data));
    for (unsigned frame = 0; frame < 1000 && gTasks[0].tState < 9; frame++)
        DoMirageTowerDisintegration(0);
    assert(gTasks[0].tState == 9);
    assert(towerUploadSize == sizeof(towerPayload));
    assert(!memcmp(towerUpload, towerPayload, 32)); // Reserved first tile unchanged.
    for (unsigned byte = 32; byte < sizeof(towerUpload); byte++) assert(towerUpload[byte] == 0);
    assert(!sFallingTower && !sMirageTowerGfxBuffer && !sMirageTowerTilemapBuffer && !sBgShakeOffsets);
    assert(allocated == 100 && freed == 100 && randomCalls == 9216 && resumed == 2 && destroyedTasks == 3);
    puts("PASS actual Mirage Tower: init/full CpuSet, 96x48 collapse bounds, reserved tile, uploads, cleanup and script resume");
}

static void fossil(void)
{
    allocated = freed = resumed = destroyed = randomCalls = animations = fossilResolved = 0;
    gTasks[0].tState = 1;
    for (unsigned state = 1; state <= 6; state++) Task_FossilFallAndSink(0);
    assert(gTasks[0].tState == 7 && sFallingFossil);
    fprintf(stderr, "fossil declared=%zu frame=%u, actual image=%zu\n",
            sizeof(sFossil_Gfx), sFallingFossil->frameImage->size, sizeof(fossilPayload));
    if (sFallingFossil->frameImage->size == sizeof(fossilPayload)) {
        assert(sFallingFossil->frameImage->data == sFallingFossil->frameImageTiles);
        assert(!memcmp(sFallingFossil->frameImageTiles, fossilPayload, sizeof(fossilPayload)));
    }
    assert(randomCalls == 1024);
    for (unsigned frame = 0; frame < 400 && sFallingFossil; frame++) {
        gSprites[0].callback(gSprites);
        if (gSprites[0].callback == SpriteCallbackDummy) {
            assert(sFallingFossil->frameImage->size == sizeof(fossilPayload));
            for (unsigned byte = 0; byte < sizeof(fossilPayload); byte++)
                assert(sFallingFossil->frameImageTiles[byte] == 0);
        }
        Task_FossilFallAndSink(0);
    }
    assert(!sFallingFossil && gTasks[0].tState == 8);
    Task_FossilFallAndSink(0);
    assert(resumed == 1 && destroyed == 1 && allocated == 4 && freed == 4 && animations == 128);
#ifdef PORT_BRIDGE
    assert(fossilResolved == 1);
#else
    assert(fossilResolved == 0);
#endif
    puts("PASS actual fossil: full image allocation/copy, fall, 256-pixel disintegration, cleanup and script resume");
}

/* Independent image oracle: pack whole 8x8 tiles from a linear 64x64 image. */
static void pack(const u8 linear[4096], u8 tiles[2048])
{
    unsigned at = 0;
    for (unsigned ty = 0; ty < 8; ty++) for (unsigned tx = 0; tx < 8; tx++)
        for (unsigned y = 0; y < 8; y++) for (unsigned x = 0; x < 8; x += 2) {
            unsigned start = (ty * 8 + y) * 64 + tx * 8 + x;
            tiles[at++] = linear[start] | (linear[start + 1] << 4);
        }
}
static void spinda(void)
{
    const u32 personalities[] = {0, UINT32_MAX, 0x87654321, 0xA5A55A5A, 0x80808080, 0x12345678};
    const u8 xy[4][2] = {{16,7},{40,8},{22,25},{34,26}};
    for (unsigned spot = 0; spot < 4; spot++) {
        assert(gSpindaSpotGraphics[spot].x == xy[spot][0]);
        assert(gSpindaSpotGraphics[spot].y == xy[spot][1]);
    }
    for (unsigned test = 0; test < ARRAY_COUNT(personalities); test++) {
        u8 linear[4096], expectedLinear[4096], original[2048], expected[2048], actual[2048];
        for (unsigned pixel = 0; pixel < 4096; pixel++) linear[pixel] = pixel % 16;
        memcpy(expectedLinear, linear, sizeof(linear));
        pack(linear, original); memcpy(actual, original, sizeof(actual));
        unsigned changed = 0;
        for (unsigned spot = 0; spot < 4; spot++) {
            unsigned traits = personalities[test] >> (spot * 8);
            int x = xy[spot][0] + (traits & 15) - 8;
            int y = xy[spot][1] + ((traits >> 4) & 15) - 8;
            for (unsigned row = 0; row < 16; row++) for (unsigned column = 0; column < 16; column++)
                if (spotImages[spot][row] & (1u << column)) {
                    int dx = x + (int)column, dy = y + (int)row;
                    assert(dx >= 0 && dx < 64 && dy >= 0 && dy < 64);
                    u8 *pixel = expectedLinear + dy * 64 + dx;
                    if (*pixel >= 1 && *pixel <= 3) { *pixel += 4; changed++; }
                }
        }
        assert(changed > 0); pack(expectedLinear, expected);
        spotsResolved = 0;
        DrawSpindaSpots(SPECIES_SPINDA, personalities[test], actual, true);
        unsigned actualChanged = 0;
        for (unsigned byte = 0; byte < sizeof(actual); byte++) {
            actualChanged += (actual[byte] & 15) != (original[byte] & 15);
            actualChanged += (actual[byte] >> 4) != (original[byte] >> 4);
        }
        fprintf(stderr, "Spinda personality %08x expected %u changed pixels, actual %u\n",
                personalities[test], changed, actualChanged);
        assert(!memcmp(expected, actual, sizeof(actual)));
#ifdef PORT_BRIDGE
        assert(spotsResolved == 4);
#endif
        spotsResolved = 0; memcpy(actual, original, sizeof(actual));
        DrawSpindaSpots(SPECIES_SPINDA, personalities[test], actual, false);
        DrawSpindaSpots(SPECIES_PIKACHU, personalities[test], actual, true);
        assert(!memcmp(original, actual, sizeof(actual)) && spotsResolved == 0);
    }
    puts("PASS actual Spinda: packed external/embedded images, all four original x/y offsets, six personalities and front/species guards");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "all") || !strcmp(argv[1], "tower")) tower();
    if (!strcmp(argv[1], "all") || !strcmp(argv[1], "fossil")) fossil();
    if (!strcmp(argv[1], "all") || !strcmp(argv[1], "spinda")) spinda();
    return 0;
}
