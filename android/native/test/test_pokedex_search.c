#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8, bool8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
enum { FALSE, TRUE, COPYWIN_GFX = 2 };
#include "dex_search_data.inc"

static bool8 national, tapPending;
static s16 tapX, tapY;
static struct { u8 queued; } sCtrDex;
static struct { s16 data[16]; void (*func)(u8); } gTasks[1];
static struct { unsigned x, y, w, h; } drawn[4];
static unsigned drawCount, lastHighlight, lastTopBar;
static bool8 IsNationalPokedexEnabled(void) { return national; }
static void SetSearchRectHighlight(u8 flags, u8 x, u8 y, u8 width)
{
    (void)flags;
    assert(drawCount < 4);
    drawn[drawCount].x = x * 8;
    drawn[drawCount].y = y * 8;
    drawn[drawCount].w = width * 8;
    drawn[drawCount++].h = 16;
}
static u8 CtrDex_Pending(void)
{ u8 result = sCtrDex.queued; sCtrDex.queued = CTR_DEX_NONE; return result; }
static bool8 CtrDex_TakeTap(s16 *x, s16 *y)
{
    if (!tapPending) return FALSE;
    *x = tapX; *y = tapY; tapPending = FALSE; return TRUE;
}
static void HighlightSelectedSearchTopBarItem(u8 bar) { lastTopBar = bar; }
static void HighlightSelectedSearchMenuItem(u8 bar, u8 item)
{ lastTopBar = bar; lastHighlight = item; }
static void CopyWindowToVram(u8 window, u8 mode) { assert(window == 0 && mode == COPYWIN_GFX); }
static void CopyBgTilemapBufferToVram(u8 bg) { assert(bg == 3); }
static void Task_SwitchToSearchMenu(u8 task) { assert(task == 0); }
#include "dex_search_helpers.inc"

static void tap(s16 x, s16 y) { tapX = x; tapY = y; tapPending = TRUE; }

static void checkDrawnItem(u8 item)
{
    drawCount = 0;
    DrawSearchMenuItemBgHighlight(item + SEARCH_TOPBAR_COUNT, FALSE, FALSE);
    assert(drawCount > 0);
    // The oracle is the actual renderer's title/selection rectangle output.
    // In particular its Hoenn OK rectangle is moved without duplicating that
    // positional adjustment in the test's expected coordinate calculation.
    for (unsigned rect = 0; rect < drawCount; rect++) {
        for (unsigned y = drawn[rect].y; y < drawn[rect].y + drawn[rect].h; y++) {
            for (unsigned x = drawn[rect].x; x < drawn[rect].x + drawn[rect].w; x++) {
                int hit = CtrDex_SearchItemAt(x, y);
                if (hit != item)
                    fprintf(stderr, "national=%u drawn item=%u at %u,%u hit=%d\n", national, item, x, y, hit);
                assert(hit == item);
            }
        }
    }
}

static void checkOkRoute(u8 topBar)
{
    drawCount = 0;
    DrawSearchMenuItemBgHighlight(SEARCH_BG_OK, FALSE, FALSE);
    assert(drawCount == 1);
    s16 x = drawn[0].x + drawn[0].w / 2;
    s16 y = drawn[0].y + drawn[0].h / 2;
    memset(gTasks, 0, sizeof(gTasks));
    gTasks[0].tTopBarItem = topBar;
    gTasks[0].tMenuItem = SEARCH_ORDER;
    sCtrDex.queued = CTR_DEX_NONE;
    lastHighlight = 0xff;
    tap(x, y);
    assert(CtrDex_SearchMenuAction(0) == CTR_DEX_CHOOSE);
    assert(gTasks[0].tMenuItem == SEARCH_OK);
    assert(lastHighlight == SEARCH_OK && lastTopBar == topBar);
    assert(CtrDex_SearchMenuAction(0) == CTR_DEX_NONE); // no repeated action

    // Opening the item directly from the top bar queues the same action for
    // the native menu task. SEARCH and SHIFT both use this path.
    gTasks[0].tMenuItem = SEARCH_ORDER;
    gTasks[0].func = NULL;
    tap(x, y);
    assert(CtrDex_SearchTopBarAction(0) == CTR_DEX_NONE);
    assert(gTasks[0].tMenuItem == SEARCH_OK);
    assert(gTasks[0].func == Task_SwitchToSearchMenu);
    assert(CtrDex_SearchMenuAction(0) == CTR_DEX_CHOOSE);
    assert(CtrDex_SearchMenuAction(0) == CTR_DEX_NONE);
}

static void checkHiddenRows(u8 topBar)
{
    gTasks[0].tTopBarItem = topBar;
    gTasks[0].tMenuItem = SEARCH_ORDER;
    for (unsigned y = 80; y < 112; y++) {
        for (unsigned x = 0; x < 136; x++) {
            int expected = national ? (y < 96 ? SEARCH_MODE : (x < 40 ? SEARCH_OK : -1))
                                    : (y < 96 && x < 40 ? SEARCH_OK : -1);
            assert(CtrDex_SearchItemAt(x, y) == expected);
            if (expected < 0) {
                tap(x, y);
                assert(CtrDex_SearchMenuAction(0) == CTR_DEX_NONE);
                assert(gTasks[0].tMenuItem == SEARCH_ORDER);
            }
        }
    }
    assert(CtrDex_SearchHasItem(topBar, SEARCH_MODE) == national);
    assert(CtrDex_SearchHasItem(topBar, SEARCH_OK));
}

int main(void)
{
    for (national = FALSE; national <= TRUE; national++) {
        checkDrawnItem(SEARCH_NAME);
        checkDrawnItem(SEARCH_COLOR);
        // TYPE's two selection rectangles meet at x88; its shared title is
        // intentionally the left type, as before the repair.
        checkDrawnItem(SEARCH_TYPE_LEFT);
        checkDrawnItem(SEARCH_TYPE_RIGHT);
        checkDrawnItem(SEARCH_ORDER);
        if (national) checkDrawnItem(SEARCH_MODE);
        checkDrawnItem(SEARCH_OK); // old code fails at the visible Hoenn OK
        for (u8 bar = SEARCH_TOPBAR_SEARCH; bar <= SEARCH_TOPBAR_SHIFT; bar++) {
            checkOkRoute(bar);
            checkHiddenRows(bar);
        }
        // SHIFT's disabled Name/Color/Type rows must not become actionable.
        gTasks[0].tTopBarItem = SEARCH_TOPBAR_SHIFT;
        gTasks[0].tMenuItem = SEARCH_ORDER;
        for (u8 item = SEARCH_NAME; item <= SEARCH_TYPE_RIGHT; item++)
            assert(!CtrDex_SearchHasItem(SEARCH_TOPBAR_SHIFT, item));
        for (s16 y = 24; y < 64; y += 16) {
            tap(60, y);
            assert(CtrDex_SearchMenuAction(0) == CTR_DEX_NONE);
            assert(gTasks[0].tMenuItem == SEARCH_ORDER);
        }
        assert(CtrDex_SearchItemAt(136, 88) == -1);
        assert(CtrDex_SearchItemAt(20, 112) == -1);
        assert(CtrDex_SearchItemAt(20, 15) == -1);
        tap(20, 8);
        assert(CtrDex_SearchMenuAction(0) == CTR_DEX_BACK);
    }
    puts("PASS real Pokedex search drawing/hit/action: Hoenn and National, SEARCH and SHIFT, visible OK, hidden rows, bounds and disabled filters");
    return 0;
}
