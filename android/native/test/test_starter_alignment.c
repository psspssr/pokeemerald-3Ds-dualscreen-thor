/* Actual starter coordinates/motion and compositor window/viewport helpers.
 * No game save, sprite asset or live device is modified. */
#include "global.h"
#include "sprite.h"
#include "task.h"
#include "window.h"
/* The real game and compositor are separate translation units. Their
 * starter flag and screen enum intentionally share a name. */
#undef CTR_CENTRED_STARTER
#include "3ds_video.h"
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>

struct Sprite gSprites[MAX_SPRITES + 1];
struct Task gTasks[NUM_TASKS];
static void Task_AskConfirmStarter(u8 taskId) { (void)taskId; }
s16 Sin(s16 index, s16 amplitude) { (void)index; (void)amplitude; return 0; }

static bool sStage, sCentred, sBattle, sTransitionCompose;
static unsigned sCentredScreen;
static int sViewX, sViewY;
static int sTargetW = CTR_GAME_WIDTH, sTargetH = CTR_GAME_HEIGHT;
static float sZoom, sOffX, sOffY, sShiftZoom;
static unsigned Min(unsigned a, unsigned b) { return a < b ? a : b; }
static unsigned Reg(unsigned reg) { (void)reg; return 0; }

#include "starter.inc"

static void configure_view(void)
{
    bool sStageRequested = false, sBattleRequested = false, sTransitionRequested = false;
    bool sLineRegs = false, sTransition;
    unsigned sCentredRequested = TEST_STARTER_CALLBACK_CENTRED ? CTR_CENTRED_STARTER : CTR_CENTRED_NONE;
#include "view.inc"
    (void)sTransition;
}

static void assert_window(const struct WindowTemplate *window)
{
    assert(window->tilemapLeft * 8 >= 0);
    assert((window->tilemapLeft + window->width) * 8 <= 240);
    assert((window->tilemapTop + window->height) * 8 <= 160);
    assert(sViewX + window->tilemapLeft * 8 >= 0);
    assert(sViewX + (window->tilemapLeft + window->width) * 8 <= CTR_GAME_WIDTH);
    assert(sViewY + window->tilemapTop * 8 >= 0);
    assert(sViewY + (window->tilemapTop + window->height) * 8 <= CTR_GAME_HEIGHT);
}

int main(void)
{
    configure_view();
    fprintf(stderr, "starter canvas=%dx%d, GBA origin=(%d,%d), bag centre=%d, selected mon target=%d\n",
            DISPLAY_WIDTH, DISPLAY_HEIGHT, sViewX, sViewY, sViewX + sPokeballCoords[1][0],
            sViewX + STARTER_PKMN_POS_X);
    /* The bag's fixed centre and DISPLAY_WIDTH-derived selection animation
     * must share native x200, then the Android presenter scales them together. */
    assert(sViewX + sPokeballCoords[1][0] == CTR_GAME_WIDTH / 2);
    assert(DISPLAY_WIDTH == 240 && DISPLAY_HEIGHT == 160);
    assert(sViewX == 80 && sViewY == 40 && sZoom == 1.0f);
    assert(TEST_STARTER_CALLBACK_CENTRED && sCentredScreen == CTR_CENTRED_STARTER);
    /* Only the repeating meadow may extend beyond the GBA picture: the bag
     * and label/text layers keep their original geometry. */
    const CentredFill *fill = &sCentredFills[CTR_CENTRED_STARTER];
    assert(fill->layers == (1u << 2) && !fill->backmost && !fill->across);
    assert_window(&sWindowTemplates[0]);
    assert_window(&sWindowTemplate_ConfirmStarter);
    puts("PASS starter callback, dialogue/confirmation and whole 240x160 picture are centred without cropping");

    for (unsigned selection = 0; selection < STARTER_MON_COUNT; ++selection) {
        struct Sprite hand = {0};
        gTasks[0].tStarterSelection = selection;
        SpriteCB_SelectionHand(&hand);
        assert(hand.x == sPokeballCoords[selection][0]);
        assert(hand.y + 32 == sPokeballCoords[selection][1]);
        assert(sViewX + hand.x >= 0 && sViewX + hand.x < CTR_GAME_WIDTH);

        struct Sprite *sprite = &gSprites[0];
        memset(sprite, 0, sizeof(*sprite));
        sprite->x = sPokeballCoords[selection][0];
        sprite->y = sPokeballCoords[selection][1];
        gTasks[0].tCircleSpriteId = 0;
        gTasks[0].func = Task_WaitForStarterSprite;
        /* It must wait for both the affine animation and movement to finish. */
        Task_WaitForStarterSprite(0);
        assert(gTasks[0].func == Task_WaitForStarterSprite);
        for (unsigned frame = 0; frame < 64; ++frame)
            SpriteCB_StarterPokemon(sprite);
        assert(sprite->x == 120 && sprite->y == 64);
        assert(sViewX + sprite->x == 200 && sViewY + sprite->y == 104);
        sprite->affineAnimEnded = true;
        Task_WaitForStarterSprite(0);
        assert(gTasks[0].func == Task_AskConfirmStarter);

        struct WindowTemplate label = sWindowTemplate_StarterLabel;
        label.tilemapLeft = sStarterLabelCoords[selection][0];
        label.tilemapTop = sStarterLabelCoords[selection][1];
        assert_window(&label);
        const int left = label.tilemapLeft * 8 - 4;
        const int right = (label.tilemapLeft + label.width) * 8 + 4;
        int first, last;
        /* Treecko's left label begins at -4, encoded as 252 in WIN0H. The
         * production centred-window decoder must preserve the wrapped edge. */
        WindowSpan(((u8)left << 8) | (u8)right, false, &first, &last);
        assert(first == left && last == right && first < last);
        assert(first + sViewX >= 0 && last + sViewX <= CTR_GAME_WIDTH);
        WindowSpan((label.tilemapTop * 8 << 8) | ((label.tilemapTop + label.height) * 8),
                   true, &first, &last);
        assert(first + sViewY >= 0 && last + sViewY <= CTR_GAME_HEIGHT);
    }
    puts("PASS all three hands, balls, labels and selection animations share the same centred geometry");
    puts("PASS Treecko's wrapped label and all starter window bounds stay visible");
    return 0;
}
