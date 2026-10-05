/* The tested windows and per-frame view calculation are extracted from the
 * generated game. The build probe supplies each real translation unit's flags. */
#include <stdbool.h>
#include "gba/types.h"
#include "gba/defines.h"
#include "window.h"
#include "3ds_video.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#define ARRAY_COUNT(a) (sizeof(a) / sizeof(*(a)))

static bool sStage, sCentred, sBattle;
static unsigned sCentredScreen;
static int sViewX, sViewY;
static float sZoom, sOffX, sOffY, sShiftZoom;
#include "item_windows.inc"

static void configure_view(void)
{
    bool sStageRequested = false, sBattleRequested = false, sTransitionRequested = false;
    bool sLineRegs = false, sTransition;
    unsigned sCentredRequested = TEST_CALLBACK_CENTRED ? CTR_CENTRED_PLAIN : CTR_CENTRED_NONE;
#include "item_view.inc"
    (void)sTransition;
}

static void assert_window(const struct WindowTemplate *window)
{
    assert(window->width && window->height);
    assert((window->tilemapLeft + window->width) * 8 <= 240);
    assert((window->tilemapTop + window->height) * 8 <= 160);
    assert(sViewX + window->tilemapLeft * 8 >= 0);
    assert(sViewX + (window->tilemapLeft + window->width) * 8 <= CTR_GAME_WIDTH);
    assert(sViewY + window->tilemapTop * 8 >= 0);
    assert(sViewY + (window->tilemapTop + window->height) * 8 <= CTR_GAME_HEIGHT);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    configure_view();
    fprintf(stderr, "%s canvas=%dx%d callback-centred=%d origin=(%d,%d)\n",
            argv[1], DISPLAY_WIDTH, DISPLAY_HEIGHT, TEST_CALLBACK_CENTRED, sViewX, sViewY);
    assert(TEST_CALLBACK_CENTRED && sCentredScreen == CTR_CENTRED_PLAIN);
    assert(DISPLAY_WIDTH == 240 && DISPLAY_HEIGHT == 160);
    assert(sViewX == 80 && sViewY == 40 && sZoom == 1.0f);
    assert(!sStage && sCentred && !sBattle);
    for (unsigned i = 0; i + 1 < ARRAY_COUNT(sWindowTemplates); i++)
        assert_window(&sWindowTemplates[i]);
#if TEST_HAS_YESNO
    assert_window(&sUsePokeblockYesNoWinTemplate);
#endif
    puts("PASS actual item-screen callback, centred GBA viewport and every dialogue/choice window bound");
    return 0;
}
