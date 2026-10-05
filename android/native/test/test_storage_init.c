/* Actual PC handlers and object definitions; only platform/drawing adapters
 * and the fallible allocator are replaced. All frees still reach ASan. */
#include <assert.h>
#include <stdlib.h>
#include "global.h"
#include "pokemon.h"
#include "window.h"
#include "mon_markings.h"
#include "task.h"
#include "malloc.h"
#include "constants/items.h"
#include "constants/rgb.h"
#include "constants/pokemon_icon.h"
#include "palette.h"
#include "pokemon_storage_system.h"
#include "pokemon_summary_screen.h"
#include "naming_screen.h"
#include "item_menu.h"
#include "storage_defs.inc"

static struct PokemonStorageSystemData *sStorage;
static struct TilemapUtil *sTilemapUtil;
static u16 sNumTilemapUtilIds;
static u8 sCurrentBoxOption, sLastUsedBox, sWhichToReshow;
static u16 sMovingItemId;
static struct Pokemon sSavedMovingMon;
struct Task gTasks[NUM_TASKS];
static void (*mainCallback)(void), (*vblankCallback)(void);
static int liveAllocations, allocationCalls, failAllocation, dirtyStorageType;
static bool8 failWindows, failMultiWindow, failMultiAllocation, dirtyItem;
static unsigned routedSummary, routedName, routedBag, resetSprites, resetCallbacks;
static unsigned loadedSavedMon, restoredSummary, gaveBagItem, initialCursor, reopenedCursor;
static unsigned initialPosition, iconFields, destroyedTasks;
static void *windowData, *multiWindowData;

#include "storage_declarations.inc"

void *Alloc(u32 size)
{
    void *p;
    allocationCalls++;
    if (allocationCalls == failAllocation || (failMultiAllocation && size == sizeof(*sMultiMove)))
        return NULL;
    p = malloc(size);
    assert(p != NULL);
    liveAllocations++;
    memset(p, 0xa5, size);
    if (size == sizeof(*sStorage))
    {
        struct PokemonStorageSystemData *storage = p;
        storage->screenChangeType = dirtyStorageType;
        memset(storage->itemIcons, 0, sizeof(storage->itemIcons));
        if (dirtyItem)
        {
            storage->itemIcons[0].active = TRUE;
            storage->itemIcons[0].area = CURSOR_AREA_IN_HAND;
            storage->movingItemId = ITEM_POTION;
        }
    }
    return p;
}

void *AllocZeroed(u32 size)
{
    void *p = Alloc(size);
    if (p != NULL)
        memset(p, 0, size);
    return p;
}

void Free(void *p)
{
    if (p != NULL)
        liveAllocations--;
    free(p);
}

static void CB2_ExitPokeStorage(void) {}
static void CB2_PokeStorage(void) {}
static void VBlankCB_PokeStorage(void) {}
static void Task_ShowPokeStorage(u8 id) { (void)id; }
static void Task_ReshowPokeStorage(u8 id) { (void)id; }
void ResetTasks(void) { memset(gTasks, 0, sizeof(gTasks)); }
u8 CreateTask(TaskFunc fn, u8 priority)
{
    assert(priority == 3);
    gTasks[0].func = fn;
    gTasks[0].isActive = TRUE;
    return 0;
}
void DestroyTask(u8 id) { gTasks[id].isActive = FALSE; destroyedTasks++; }
void SetMainCallback2(void (*fn)(void)) { mainCallback = fn; }
void SetVBlankCallback(void (*fn)(void)) { vblankCallback = fn; }
u8 StorageGetCurrentBox(void) { return 7; }
static void SetGpuReg(u8 reg, u16 value) { (void)reg; (void)value; }
static void ResetForPokeStorage(void)
{
    /* Case 0's real allocation; the omitted calls reset graphics only. */
    TilemapUtil_Init(TILEMAPID_COUNT);
    sStorage->closeBoxFlashing = FALSE;
}
static void LoadSavedMovingMon(void) { loadedSavedMon++; }
static void SetSelectionAfterSummaryScreen(void) { restoredSummary++; }
static void GiveChosenBagItem(void) { gaveBagItem++; }
static void LoadPokeStorageMenuGfx(void) {}
static void LoadWaveformSpritePalette(void) {}
static bool8 InitPokeStorageWindows(void)
{
    /* Model a partial InitWindows allocation before its failure return. */
    windowData = Alloc(64);
    return windowData != NULL && !failWindows;
}
void PutWindowTilemap(u8 window) { (void)window; }
void ClearWindowTilemap(u8 window) { (void)window; }
#undef CpuFill32
#define CpuFill32(value, destination, size) ((void)0)
static void LoadUserWindowBorderGfx(u8 id, u16 base, u8 palette) {}
static void ResetAllBgCoords(void) {}
static void InitStartingPosData(void) { initialPosition++; }
static void InitMonIconFields(void) { iconFields++; }
static void InitCursor(void) { initialCursor++; }
static void InitCursorOnReopen(void) { reopenedCursor++; }
static void SetScrollingBackground(void) {}
static void InitPokeStorageBg0(void) {}
static void InitPalettesAndSprites(void) {}
static void InitSupplementalTilemaps(void) {}
static void CreateInitBoxTask(u8 box) { assert(box == 7); }
static bool8 IsInitBoxActive(void) { return FALSE; }
void InitMonMarkingsMenu(struct MonMarkingsMenu *menu) { (void)menu; }
void BufferMonMarkingsMenuTiles(void) {}
static void CreateItemIconSprites(void) {}
static void InitCursorItemIcon(void)
{
    if (sMovingItemId != ITEM_NONE)
    {
        sStorage->itemIcons[0].active = TRUE;
        sStorage->itemIcons[0].area = CURSOR_AREA_IN_HAND;
        sStorage->movingItemId = sMovingItemId;
    }
}
static void SetMonIconTransparency(void) {}
void BlendPalettes(u32 palettes, u8 coeff, u16 color) {}
u16 AddWindow8Bit(const struct WindowTemplate *window)
{
    assert(window == &sWindowTemplate_MultiMove);
    multiWindowData = Alloc(96);
    return multiWindowData != NULL && !failMultiWindow ? 4 : WINDOW_NONE;
}
void FillWindowPixelBuffer(u8 id, u8 color) { assert(id == 4 && color == 0); }
void FreeAllWindowBuffers(void)
{
    FREE_AND_SET_NULL(windowData);
    FREE_AND_SET_NULL(multiWindowData);
}
void ResetSpriteData(void) { resetSprites++; }
void SetVBlankHBlankCallbacksToNull(void) { resetCallbacks++; vblankCallback = NULL; }
void ShowPokemonSummaryScreen(u8 mode, void *mon, u8 index, u8 maxIndex, void (*cb)(void))
{
    assert(cb == CB2_ReturnToPokeStorage);
    routedSummary++;
}
void ShowPokemonSummaryScreenHandleDeoxys(u8 mode, struct BoxPokemon *mon, u8 index, u8 maxIndex, void (*cb)(void))
{
    ShowPokemonSummaryScreen(mode, mon, index, maxIndex, cb);
}
u8 *GetBoxNamePtr(u8 box) { static u8 name[16]; assert(box == 7); return name; }
void DoNamingScreen(u8 nameTemplate, u8 *dest, u16 species, u16 gender, u32 personality, void (*cb)(void))
{
    assert(nameTemplate == NAMING_SCREEN_BOX && cb == CB2_ReturnToPokeStorage);
    routedName++;
}
void GoToBagMenu(u8 type, u8 pocket, void (*cb)(void))
{
    assert(type == ITEMMENULOCATION_PCBOX && pocket == 0 && cb == CB2_ReturnToPokeStorage);
    routedBag++;
}

#include "storage_helpers.inc"

static void resetTest(void)
{
    assert(liveAllocations == 0 && sStorage == NULL);
    /* Leave the production-owned sMultiMove pointer intact between visits. */
    allocationCalls = failAllocation = 0;
    dirtyStorageType = SCREEN_CHANGE_EXIT_BOX;
    failWindows = failMultiWindow = failMultiAllocation = dirtyItem = FALSE;
    mainCallback = NULL;
    routedSummary = routedName = routedBag = 0;
    loadedSavedMon = restoredSummary = gaveBagItem = 0;
    initialCursor = reopenedCursor = initialPosition = iconFields = 0;
    resetSprites = resetCallbacks = destroyedTasks = 0;
    sMovingItemId = ITEM_NONE;
}

static void beginVisit(bool8 reopening, u8 option)
{
    if (reopening)
    {
        sCurrentBoxOption = option;
        CB2_ReturnToPokeStorage();
    }
    else
        EnterPokeStorage(option);
    assert(sStorage != NULL && sStorage->boxOption == option);
    assert(sStorage->isReopening == reopening && sStorage->state == 0);
    assert(mainCallback == CB2_PokeStorage && gTasks[0].func == Task_InitPokeStorage);
    if (!reopening)
        assert(sLastUsedBox == 7);
}

static void driveInit(void)
{
    unsigned frames = 0;
    while (gTasks[0].func == Task_InitPokeStorage && frames++ < 16)
        gTasks[0].func(0);
    assert(frames <= 11);
}

static void assertClosed(void)
{
    assert(mainCallback == CB2_ExitPokeStorage);
    assert(routedSummary == 0 && routedName == 0 && routedBag == 0);
    assert(sStorage == NULL && liveAllocations == 0);
    assert(!gTasks[0].isActive && destroyedTasks == 1);
    assert(resetSprites == 1 && resetCallbacks == 1 && vblankCallback == NULL);
}

static void windowFailure(void)
{
    for (u8 reopening = 0; reopening < 2; reopening++)
        for (u8 oldScreen = SCREEN_CHANGE_SUMMARY_SCREEN; oldScreen <= SCREEN_CHANGE_ITEM_FROM_BAG; oldScreen++)
        {
            resetTest();
            dirtyStorageType = oldScreen;
            failWindows = TRUE;
            beginVisit(reopening, OPTION_MOVE_MONS);
            driveInit();
            assert(gTasks[0].func == Task_ChangeScreen);
            gTasks[0].func(0);
            assertClosed();
        }
    puts("PASS PC entry/return with dirty prior screen and failed windows exits safely");
}

static void itemStateFailure(void)
{
    for (u8 reopening = 0; reopening < 2; reopening++)
    {
        resetTest();
        dirtyItem = TRUE;
        failWindows = TRUE;
        beginVisit(reopening, OPTION_MOVE_ITEMS);
        driveInit();
        gTasks[0].func(0);
        assert(sMovingItemId == ITEM_NONE);
        assertClosed();
    }
    puts("PASS PC failure cannot carry an uninitialized held-item record");
}

static void normalVisits(void)
{
    for (u8 option = OPTION_WITHDRAW; option <= OPTION_MOVE_ITEMS; option++)
        for (u8 reopening = 0; reopening < 2; reopening++)
            for (u8 reshow = 0; reshow < 3; reshow++)
            {
                resetTest();
                sWhichToReshow = reshow;
                beginVisit(reopening, option);
                driveInit();
                assert(gTasks[0].func == (reopening ? Task_ReshowPokeStorage : Task_ShowPokeStorage));
                assert(initialCursor == !reopening && reopenedCursor == reopening);
                assert(initialPosition == !reopening && iconFields == 1);
                assert(loadedSavedMon == (reopening && reshow == SCREEN_CHANGE_NAME_BOX - 1));
                assert(restoredSummary == (reopening && reshow == SCREEN_CHANGE_SUMMARY_SCREEN - 1));
                assert(gaveBagItem == (reopening && reshow == SCREEN_CHANGE_ITEM_FROM_BAG - 1));
                assert(vblankCallback == VBlankCB_PokeStorage);
                sStorage->screenChangeType = SCREEN_CHANGE_EXIT_BOX;
                Task_ChangeScreen(0);
                assertClosed();
            }
    puts("PASS normal PC entry/return preserves all options and restore callbacks");
}

static void repeatCleanup(void)
{
    for (unsigned repeat = 0; repeat < 3; repeat++)
    {
        resetTest();
        beginVisit(FALSE, OPTION_MOVE_MONS);
        driveInit();
        Task_ChangeScreen(0);
        assertClosed();
        resetTest();
        failWindows = TRUE;
        beginVisit(FALSE, OPTION_MOVE_MONS);
        driveInit();
        gTasks[0].func(0);
        assertClosed();
    }
    assert(sMultiMove == NULL);
    puts("PASS PC exit then early failure does not double-free the previous move buffer");
}

static void normalDispatch(void)
{
    for (u8 screen = SCREEN_CHANGE_SUMMARY_SCREEN; screen <= SCREEN_CHANGE_ITEM_FROM_BAG; screen++)
    {
        resetTest();
        beginVisit(FALSE, OPTION_MOVE_ITEMS);
        driveInit();
        sStorage->screenChangeType = screen;
        sStorage->summaryMon.box = &sSavedMovingMon.box;
        sStorage->summaryScreenMode = SUMMARY_MODE_NORMAL;
        sStorage->summaryStartPos = sStorage->summaryMaxPos = 0;
        sStorage->itemIcons[0].active = TRUE;
        sStorage->itemIcons[0].area = CURSOR_AREA_IN_HAND;
        sStorage->movingItemId = ITEM_POTION;
        Task_ChangeScreen(0);
        assert(sStorage == NULL && liveAllocations == 0 && sMovingItemId == ITEM_POTION);
        assert(routedSummary == (screen == SCREEN_CHANGE_SUMMARY_SCREEN));
        assert(routedName == (screen == SCREEN_CHANGE_NAME_BOX));
        assert(routedBag == (screen == SCREEN_CHANGE_ITEM_FROM_BAG));
        sWhichToReshow = screen - 1;
        beginVisit(TRUE, OPTION_MOVE_ITEMS);
        driveInit();
        assert(gTasks[0].func == Task_ReshowPokeStorage);
        assert(IsMovingItem() && GetMovingItemId() == ITEM_POTION);
        /* A normal user places the held item before leaving the PC. */
        sStorage->itemIcons[0].active = FALSE;
        sStorage->screenChangeType = SCREEN_CHANGE_EXIT_BOX;
        Task_ChangeScreen(0);
        assert(sStorage == NULL && liveAllocations == 0 && sMovingItemId == ITEM_NONE);
    }
    puts("PASS explicit Summary/name/bag dispatch and held-item restoration are unchanged");
}

static void allocationFailures(void)
{
    for (u8 reopening = 0; reopening < 2; reopening++)
    {
        resetTest();
        failAllocation = 1;
        sCurrentBoxOption = OPTION_MOVE_MONS;
        if (reopening) CB2_ReturnToPokeStorage();
        else EnterPokeStorage(OPTION_MOVE_MONS);
        assert(sStorage == NULL && liveAllocations == 0 && mainCallback == CB2_ExitPokeStorage);
        for (u8 failure = 0; failure < 2; failure++)
        {
            resetTest();
            dirtyStorageType = SCREEN_CHANGE_NAME_BOX;
            failMultiAllocation = failure == 0;
            failMultiWindow = failure == 1;
            beginVisit(reopening, OPTION_MOVE_MONS);
            driveInit();
            assert(gTasks[0].func == Task_ChangeScreen);
            gTasks[0].func(0);
            assertClosed();
        }
    }
    puts("PASS storage/move-buffer/move-window allocation failures release partial objects");
}

int main(int argc, char **argv)
{
    const char *test = argc == 2 ? argv[1] : "all";
#define RUN(name, fn) if (!strcmp(test, "all") || !strcmp(test, name)) fn()
    RUN("window", windowFailure);
    RUN("item-state", itemStateFailure);
    RUN("cleanup", repeatCleanup);
    RUN("normal", normalVisits);
    RUN("dispatch", normalDispatch);
    RUN("allocation", allocationFailures);
    assert(liveAllocations == 0);
    return 0;
}
