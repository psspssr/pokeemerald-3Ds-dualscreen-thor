#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8, bool8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t s16;
typedef void (*MainCallback)(void);
enum { FALSE, TRUE, DISPLAY_WIDTH = 240, DISPLAY_HEIGHT = 160, TASK_NONE = 255, SE_SELECT = 1 };
enum { A_BUTTON = 1, B_BUTTON = 2, SELECT_BUTTON = 4, START_BUTTON = 8 };
#include "naming_data.inc"
struct Task { s16 data[16]; };
static struct Task gTasks[1];
static struct NamingScreenData naming, *sNamingScreen;
static struct { MainCallback callback2; u32 vblankCounter1; u16 newKeys; } gMain;
static struct { bool active; } gPaletteFade;
static bool taskExists, printerActive, textFull, allocationFails;
static unsigned writes, deletes, movement, flashes, sounds, squishes, timers;
static s16 cursorX, cursorY;
static void Task_HandleInput(u8 taskId);
static void CB2_NamingScreen(void) {}
static void CB2_LoadNamingScreen(void) {}
static void OtherCallback(void) {}
static u8 FindTaskIdByFunc(void (*fn)(u8))
{ assert(fn == Task_HandleInput); return taskExists ? 0 : TASK_NONE; }
static bool IsTextPrinterActive(u8 window) { assert(window == 0); return printerActive; }
static void SetCursorPos(s16 x, s16 y) { cursorX = x; cursorY = y; }
static void GetCursorPos(s16 *x, s16 *y) { *x = cursorX; *y = cursorY; }
static void HandleDpadMovement(struct Task *task) { (void)task; movement++; }
static bool8 AddTextCharacter(void) { writes++; return textFull; }
static void DeleteTextCharacter(void) { deletes++; }
static void TryStartButtonFlash(u8 button, bool8 keep, bool8 interrupt)
{ assert(button <= BUTTON_COUNT); (void)keep; (void)interrupt; flashes++; }
static void SquishCursor(void) { squishes++; }
static void PlaySE(u16 sound) { assert(sound == SE_SELECT); sounds++; }
static void RunTextPrinters(void) {}
static void *Alloc(size_t bytes) { return allocationFails ? NULL : calloc(1, bytes); }
static void StartTimer1(void) { timers++; }
static void SetMainCallback2(MainCallback callback) { gMain.callback2 = callback; }
#define JOY_NEW(button) (gMain.newKeys & (button))
#include "naming_helpers.inc"

static void ready(unsigned page)
{
    memset(&naming, 0, sizeof(naming)); memset(gTasks, 0, sizeof(gTasks));
    memset(&sCtrNamingTap, 0, sizeof(sCtrNamingTap));
    naming.state = STATE_HANDLE_INPUT; naming.currentPage = page; sNamingScreen = &naming;
    gMain.callback2 = CB2_NamingScreen; gMain.vblankCounter1 = 100; gMain.newKeys = 0;
    gPaletteFade.active = printerActive = textFull = false; taskExists = true;
    gTasks[0].tState = INPUT_STATE_ENABLED; cursorX = cursorY = 0;
    writes = deletes = movement = flashes = sounds = squishes = 0;
}
static void frame(void) { Task_HandleInput(0); HandleKeyboardEvent(); }
static void tap(int x, int y) { CtrNaming_Tap(x, y); frame(); }

int main(void)
{
    for (unsigned page = 0; page < KBPAGE_COUNT; page++) {
        ready(page);
        unsigned id = CurrentPageToKeyboardId(), columns = GetCurrentPageColumnCount();
        for (unsigned row = 0; row < 4; row++) for (unsigned col = 0; col < columns; col++) {
            unsigned old = writes;
            tap(38 + sPageColumnXPos[id][col], 88 + row * 16);
            assert(writes == old + 1 && cursorX == (int)col && cursorY == (int)row);
            frame(); assert(writes == old + 1); // a held/stale frame never repeats a character
        }
        unsigned old = writes;
        const int outside[][2] = {{-1,88},{240,88},{38,-1},{38,160},{38,79},{38,144},{178,88},{226,140}};
        for (unsigned i = 0; i < sizeof(outside)/sizeof(*outside); i++) tap(outside[i][0], outside[i][1]);
        assert(writes == old);
        if (id != KEYBOARD_SYMBOLS) { tap(79,88); tap(145,88); assert(writes == old); }
        tap(204,116); assert(deletes == 1 && writes == old);
        tap(204,88); assert(naming.state == STATE_START_PAGE_SWAP && writes == old);
        CtrNaming_Tap(38,88); assert(!sCtrNamingTap.pending);
        ready(page); tap(204,140); assert(naming.state == STATE_PRESSED_OK && !writes && sounds == 1);
    }
    ready(KBPAGE_LETTERS_UPPER); textFull = true; tap(38,88);
    assert(writes == 1 && naming.state == STATE_MOVE_TO_OK_BUTTON && gTasks[0].tState == INPUT_STATE_OVERRIDE);
    CtrNaming_Tap(50,88); assert(!sCtrNamingTap.pending); frame(); assert(writes == 1);
    // Controller A/B/Select/Start use the original handlers and cursor roles.
    ready(KBPAGE_LETTERS_UPPER); gMain.newKeys = A_BUTTON; frame(); assert(writes == 1);
    gMain.newKeys = B_BUTTON; frame(); assert(deletes == 1);
    gMain.newKeys = START_BUTTON; frame(); assert(cursorX == 8 && cursorY == BUTTON_OK);
    gMain.newKeys = SELECT_BUTTON; frame(); assert(naming.state == STATE_START_PAGE_SWAP);
    // Pending taps cannot cross callback, page, state, fade, task or frame gaps.
    for (unsigned invalid = 0; invalid < 7; invalid++) {
        ready(KBPAGE_LETTERS_UPPER); CtrNaming_Tap(38,88); assert(sCtrNamingTap.pending);
        if (invalid == 0) gMain.callback2 = OtherCallback;
        if (invalid == 1) naming.currentPage = KBPAGE_LETTERS_LOWER;
        if (invalid == 2) naming.state = STATE_WAIT_PAGE_SWAP;
        if (invalid == 3) gPaletteFade.active = true;
        if (invalid == 4) taskExists = false;
        if (invalid == 5) gMain.vblankCounter1 += 31;
        if (invalid == 6) sNamingScreen = NULL;
        s16 x, y; assert(!CtrNaming_TakeTap(&x,&y) && !sCtrNamingTap.pending);
    }
    ready(KBPAGE_LETTERS_UPPER); CtrNaming_Tap(38,88); SetInputState(INPUT_STATE_DISABLED);
    assert(!sCtrNamingTap.pending); Task_HandleInput(0); assert(gTasks[0].tKeyboardEvent == INPUT_NONE);
    // The post-capture transfer message can be acknowledged by touch only
    // once printing finishes; the tap cannot type into a later keyboard.
    ready(KBPAGE_LETTERS_UPPER); naming.state = STATE_WAIT_SENT_TO_PC_MESSAGE;
    SetInputState(INPUT_STATE_DISABLED); printerActive = true;
    CtrNaming_Tap(120,135); assert(!sCtrNamingTap.pending);
    MainState_WaitSentToPCMessage(); assert(naming.state == STATE_WAIT_SENT_TO_PC_MESSAGE);
    printerActive = false; CtrNaming_Tap(120,135); Task_HandleInput(0);
    MainState_WaitSentToPCMessage(); assert(naming.state == STATE_FADE_OUT && !writes);
    ready(KBPAGE_LETTERS_UPPER); naming.state = STATE_WAIT_SENT_TO_PC_MESSAGE;
    gMain.newKeys = A_BUTTON; MainState_WaitSentToPCMessage(); assert(naming.state == STATE_FADE_OUT);
    // Every naming template opens without retaining input from an older scene.
    u8 text[16] = {0xff};
    for (unsigned template = NAMING_SCREEN_PLAYER; template <= NAMING_SCREEN_WALDA; template++) {
        ready(KBPAGE_LETTERS_UPPER); CtrNaming_Tap(38,88);
        DoNamingScreen(template, text, 1, 0, 123, OtherCallback);
        assert(!sCtrNamingTap.pending && !CtrNaming_IsOpen() && gMain.callback2 == CB2_LoadNamingScreen);
        assert(sNamingScreen->templateNum == template && sNamingScreen->destBuffer == text);
        free(sNamingScreen); sNamingScreen = NULL;
    }
    allocationFails = true; DoNamingScreen(NAMING_SCREEN_BOX,text,1,0,0,OtherCallback);
    assert(!sNamingScreen && gMain.callback2 == OtherCallback && !CtrNaming_IsOpen());
    assert(timers == 1);
    puts("PASS naming: all real page/key positions and native actions, max-name transition, controllers, lifecycle rejection, transfer acknowledgement and all five template opens");
    return 0;
}
