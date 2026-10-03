#include "global.h"
#include "sprite.h"
#include "trig.h"
#include "random.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct Sprite gSprites[MAX_SPRITES + 1];
static const struct SpriteTemplate sEvoSparkleSpriteTemplate = {0};

u8 CreateSprite(const struct SpriteTemplate *template, s16 x, s16 y, u8 priority)
{
    assert(template == &sEvoSparkleSpriteTemplate && priority == 0);
    memset(&gSprites[0], 0, sizeof(gSprites[0]));
    gSprites[0].x = x;
    gSprites[0].y = y;
    return 0;
}

void DestroySprite(struct Sprite *sprite) { (void)sprite; }
u16 Random(void) { return 0; }
s16 Sin(s16 index, s16 amplitude) { (void)index; (void)amplitude; return 0; }
s16 Cos(s16 index, s16 amplitude) { (void)index; (void)amplitude; return 0; }

// Factories and update callbacks extracted unchanged from evolution_graphics.c.
#include "sparkles.inc"

static void expect_center(s16 y)
{
    fprintf(stderr, "sparkle origin=(%d,%d), expected=(120,%d)\n",
            gSprites[0].x, gSprites[0].y, y);
    assert(gSprites[0].x == 120 && gSprites[0].y == y);
    // The centred compositor adds 80 to both the hard-coded mon x120 and
    // these DISPLAY_WIDTH/2 helpers; all must end up at native x200.
    assert(gSprites[0].x + 80 == 200);
}

int main(void)
{
    CreateSparkle_SpiralUpward(0);
    expect_center(88);
    gSprites[0].callback(&gSprites[0]);
    expect_center(88);

    CreateSparkle_ArcDown(0);
    expect_center(8);
    gSprites[0].callback(&gSprites[0]);
    expect_center(8);

    CreateSparkle_CircleInward(0, 2);
    expect_center(56);
    gSprites[0].callback(&gSprites[0]);
    expect_center(56);

    CreateSparkle_Spray(0);
    expect_center(56);
    gSprites[0].sTimer = 9;
    gSprites[0].sSpeed = 3;
    gSprites[0].callback(&gSprites[0]);
    assert(gSprites[0].x == 129 && gSprites[0].y == 56);
    gSprites[0].sTimer = 12;
    gSprites[0].sSpeed = -3;
    gSprites[0].callback(&gSprites[0]);
    assert(gSprites[0].x == 108 && gSprites[0].y == 57);
    puts("PASS: all four evolution sparkle origins and spray motion share the mon centre");
    return 0;
}
