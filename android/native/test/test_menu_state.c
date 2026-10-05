#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8, bool8;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
enum { FALSE, TRUE };
enum { FONT_NORMAL, SE_SELECT, MENU_CURSOR_DELTA_NONE = 0 };
enum { WINDOW_TILEMAP_LEFT, WINDOW_TILEMAP_TOP, WINDOW_WIDTH };
#define PIXEL_FILL(n) (n)
static const u8 gText_SelectorArrow3[] = {0xff};
#include "menu_data.inc"

static unsigned windowLeft, windowTop, windowWidth, cursorX, cursorY, sounds;
static unsigned GetWindowAttribute(u8 window, unsigned attr)
{
    assert(window == 1);
    return attr == WINDOW_TILEMAP_LEFT ? windowLeft : attr == WINDOW_TILEMAP_TOP ? windowTop : windowWidth;
}
static u8 GetMenuCursorDimensionByFont(u8 font, u8 dimension)
{ (void)font; return dimension ? 16 : 8; }
static void FillWindowPixelRect(u8 window, u8 color, u8 x, u8 y, u8 width, u8 height)
{ (void)window; (void)color; (void)x; (void)y; (void)width; (void)height; }
static void AddTextPrinterParameterized(u8 window, u8 font, const u8 *text, u8 x, u8 y, u8 speed, void *callback)
{ (void)window; (void)font; (void)text; (void)speed; (void)callback; cursorX = x; cursorY = y; }
static void PlaySE(unsigned effect) { assert(effect == SE_SELECT); sounds++; }
#include "menu_helpers.inc"

static void bagGrid(unsigned rows)
{
    // Real Bag context-menu initializer and spacing; no handcrafted sMenu.
    windowLeft = 14; windowTop = 12; windowWidth = 15;
    InitMenuActionGrid(1, 56, 2, rows, 0);
    for (unsigned row = 0; row < rows; row++) {
        for (unsigned col = 0; col < 2; col++) {
            int entry = CtrMenu_EntryAt(112 + 16 + col * 56, 97 + 8 + row * 16);
            assert(entry == (int)(row * 2 + col));
            assert(CtrMenu_Choose(entry, FALSE) == entry);
            assert(cursorX == col * 56 && cursorY == 1 + row * 16);
        }
    }
}

static void vertical(unsigned count, bool upperLeft)
{
    windowLeft = 20; windowTop = 3; windowWidth = 9;
    unsigned left = upperLeft ? 0 : 8, top = upperLeft ? 1 : 3;
    unsigned pitch = upperLeft ? 16 : 14;
    if (upperLeft) InitMenuInUpperLeftCorner(1, count, 0, FALSE);
    else InitMenu(1, FONT_NORMAL, left, top, pitch, count, 0, TRUE);
    // Test the actual physical menu's row centers and the rendered cursor.
    // Before083, Summary's second row becomes Store and Mark is outside.
    for (unsigned row = 0; row < count; row++) {
        int entry = CtrMenu_EntryAt(192, 24 + top + pitch * row + pitch / 2);
        if (entry != (int)row) fprintf(stderr, "vertical row%u became%d after Bag grid\n", row, entry);
        assert(entry == (int)row);
        unsigned oldSounds = sounds;
        assert(CtrMenu_Choose(entry, TRUE) == entry);
        assert(cursorX == left && cursorY == top + pitch * row);
        assert(sounds == oldSounds + (upperLeft ? 1 : 0));
    }
    assert(CtrMenu_EntryAt(159, 40) == -1);
    assert(CtrMenu_EntryAt(232, 40) == -1);
    assert(CtrMenu_EntryAt(192, 24 + top - 1) == -1);
    assert(CtrMenu_EntryAt(192, 24 + top + pitch * count) == -1);
    // Controller cursor behavior continues to use the same vertical bounds.
    Menu_MoveCursor(1); assert(sMenu.cursorPos == 0);
    Menu_MoveCursorNoWrapAround(-1); assert(sMenu.cursorPos == 0);
}

int main(void)
{
    // Storage six-action menu and two-choice confirmations after both Bag
    // shapes, plus InitMenu used by other vertical game menus.
    for (unsigned rows = 2; rows <= 3; rows++) {
        bagGrid(rows); vertical(6, true);
        bagGrid(rows); vertical(2, true);
        bagGrid(rows); vertical(4, false);
    }
    // The general grid initializer still uses its own width/height/columns.
    windowLeft = 0; windowTop = 0; windowWidth = 24;
    InitMenuGrid(1, FONT_NORMAL, 0, 2, 48, 20, 3, 2, 6, 0);
    assert(CtrMenu_EntryAt(110, 28) == 5);
    assert(CtrMenu_Choose(5, FALSE) == 5);
    assert(cursorX == 96 && cursorY == 22);
    vertical(6, false);
    bagGrid(2); // returning to Bag remains a grid after every vertical route
    puts("PASS menu state: real Bag grids -> PC/yes-no/general vertical hit testing, cursor and sound; grid reuse unchanged");
    return 0;
}
