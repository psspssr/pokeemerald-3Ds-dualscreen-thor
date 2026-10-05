/* The fixture uses synthetic CreateMon records and the actual Pokemon/RNG
 * translation units, plus extracted PC mutation/return/save-party handlers. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "pokemon.h"
#include "battle.h"
#include "pokemon_storage_system.h"
#include "random.h"
#include "text.h"
#include "main.h"
#include "menu.h"
#include "item.h"
#include "palette.h"
#include "constants/battle_pyramid.h"
#include "constants/field_weather.h"
#include "constants/region_map_sections.h"
#include "constants/songs.h"

_Static_assert(sizeof(struct Pokemon) == 100 && sizeof(struct BoxPokemon) == 80, "GBA record ABI");
static struct SaveBlock1 saveBlock;
static struct SaveBlock2 trainer;
static struct PokemonStorage boxes;
struct SaveBlock1 *gSaveBlock1Ptr = &saveBlock;
struct SaveBlock2 *gSaveBlock2Ptr = &trainer;
struct PokemonStorage *gPokemonStoragePtr = &boxes;
struct Main gMain;
u32 gBattleTypeFlags;
struct BattleScripting gBattleScripting;
struct PaletteFadeControl gPaletteFade;
struct BagPocket gBagPockets[POCKETS_COUNT];
static bool8 sIsMonBeingMoved, sAutoActionOn, sInPartyMenu;
static u8 sMovingMonOrigBoxId, sMovingMonOrigBoxPos;
static s8 sCursorArea, sCursorPosition;
u8 gLastViewedMonIndex;
const u8 gGameLanguage = LANGUAGE_ENGLISH, gGameVersion = VERSION_EMERALD;
const u8 gText_BadEgg[] = {EOS}, gText_EggNickname[] = {EOS};
const u8 gSpeciesNames[NUM_SPECIES + 1][POKEMON_NAME_LENGTH + 1] = {
    [SPECIES_TORCHIC] = {0xce, 0xc9, 0xcc, 0xbd, 0xc2, 0xc3, 0xbd, 0xa2, 0xa3, 0xa4, EOS},
};
const u32 gBitTable[32] = {
    1u<<0, 1u<<1, 1u<<2, 1u<<3, 1u<<4, 1u<<5, 1u<<6, 1u<<7,
    1u<<8, 1u<<9, 1u<<10, 1u<<11, 1u<<12, 1u<<13, 1u<<14, 1u<<15,
    1u<<16, 1u<<17, 1u<<18, 1u<<19, 1u<<20, 1u<<21, 1u<<22, 1u<<23,
    1u<<24, 1u<<25, 1u<<26, 1u<<27, 1u<<28, 1u<<29, 1u<<30, 1u<<31,
};
u32 CtrQol_WildPersonality(struct BoxPokemon *mon, u16 species, u32 personality, unsigned otIdType) { return personality; }
u8 GetCurrentRegionMapSectionId(void) { return MAPSEC_ROUTE_101; }
u8 *StringCopy(u8 *dest, const u8 *src) { while ((*dest++ = *src++) != EOS) {} return dest - 1; }
u16 StringLength(const u8 *s) { const u8 *p = s; while (*p != EOS) p++; return p - s; }

/* global.h's game config defines NDEBUG; all fixture assertions stay active. */
#undef NDEBUG
#include <assert.h>
#include "storage_platform.inc"

static struct Pokemon expectedParty[PARTY_SIZE];
static struct PokemonStorage expectedBoxes;
static struct SaveBlock1 expectedSave;
static u8 expectedPartyCount;

static void makeMon(struct Pokemon *mon, unsigned id)
{
    CreateMon(mon, SPECIES_TORCHIC, 12, 31, TRUE, 0x12345000 + id, OT_ID_PLAYER_ID, 0);
    assert(GetMonData(mon, MON_DATA_SPECIES) == SPECIES_TORCHIC);
    assert(!GetMonData(mon, MON_DATA_SANITY_IS_BAD_EGG));
}

static void setup(unsigned partyCount)
{
    resetTest();
    memset(&saveBlock, 0, sizeof(saveBlock));
    memset(&trainer, 0, sizeof(trainer));
    memset(&boxes, 0, sizeof(boxes));
    memset(gPlayerParty, 0, sizeof(expectedParty));
    memset(&gMain, 0, sizeof(gMain));
    memset(&gPaletteFade, 0, sizeof(gPaletteFade));
    trainer.playerTrainerId[0] = 0x41;
    memset(trainer.playerName, EOS, sizeof(trainer.playerName));
    trainer.encryptionKey = 0x12345678;
    trainer.playTimeHours = 19;
    saveBlock.money = 0x55aa55aa;
    saveBlock.location.mapNum = 37;
    boxes.currentBox = 7;
    SeedRng(1234);
    for (unsigned i = 0; i < partyCount; i++) makeMon(&gPlayerParty[i], i + 1);
    gPlayerPartyCount = partyCount;
    struct Pokemon mon;
    for (unsigned i = 0; i < 5; i++) { makeMon(&mon, 20 + i); boxes.boxes[7][i] = mon.box; }
    gBagPockets[ITEMS_POCKET] = (struct BagPocket){saveBlock.bagPocket_Items, BAG_ITEMS_COUNT};
    gBagPockets[KEYITEMS_POCKET] = (struct BagPocket){saveBlock.bagPocket_KeyItems, BAG_KEYITEMS_COUNT};
    gBagPockets[BALLS_POCKET] = (struct BagPocket){saveBlock.bagPocket_PokeBalls, BAG_POKEBALLS_COUNT};
    gBagPockets[TMHM_POCKET] = (struct BagPocket){saveBlock.bagPocket_TMHM, BAG_TMHM_COUNT};
    gBagPockets[BERRIES_POCKET] = (struct BagPocket){saveBlock.bagPocket_Berries, BAG_BERRIES_COUNT};
    for (unsigned p = 0; p < POCKETS_COUNT; p++)
        for (unsigned i = 0; i < gBagPockets[p].capacity; i++)
            SetBagItemQuantity(&gBagPockets[p].itemSlots[i].quantity, 0);
    sIsMonBeingMoved = sAutoActionOn = FALSE;
    sWhichToReshow = 0;
#ifdef STORAGE_CARRY_PATCHED
    assert(!sStorageCarry.active);
    sStorageCarryRecovered = FALSE;
    sStorageInitFailed = FALSE;
#endif
}

static void remember(void)
{
    memcpy(expectedParty, gPlayerParty, sizeof(expectedParty));
    expectedBoxes = boxes;
    expectedSave = saveBlock;
    expectedPartyCount = 0;
    while (expectedPartyCount < PARTY_SIZE && GetMonData(&expectedParty[expectedPartyCount], MON_DATA_SPECIES))
        expectedPartyCount++;
}

static void verifyRestored(void)
{
    assert(memcmp(expectedParty, gPlayerParty, sizeof(expectedParty)) == 0 && "carried Pokemon/held items must be conserved");
    assert(memcmp(expectedBoxes.boxes, boxes.boxes, sizeof(boxes.boxes)) == 0);
    assert(memcmp(expectedSave.mail, saveBlock.mail, sizeof(saveBlock.mail)) == 0);
#define CHECK(member) assert(memcmp(expectedSave.member, saveBlock.member, sizeof(saveBlock.member)) == 0)
    CHECK(bagPocket_Items); CHECK(bagPocket_KeyItems); CHECK(bagPocket_PokeBalls); CHECK(bagPocket_TMHM); CHECK(bagPocket_Berries);
#undef CHECK
    assert(gPlayerPartyCount == expectedPartyCount);
    assert(!sIsMonBeingMoved && sMovingItemId == ITEM_NONE);
    assert(saveBlock.money == expectedSave.money && saveBlock.location.mapNum == expectedSave.location.mapNum);
    assert(trainer.playTimeHours == 20); /* time advanced after the checkpoint */
    SavePlayerParty();
    assert(saveBlock.playerPartyCount == expectedPartyCount);
    assert(memcmp(saveBlock.playerParty, expectedParty, sizeof(expectedParty)) == 0);
#ifdef STORAGE_CARRY_PATCHED
    assert(!sStorageCarry.active && sStorageCarryRecovered);
#endif
}

static void pickMon(u8 area, u8 pos)
{
    sCursorArea = area; sCursorPosition = pos;
    MoveMon();
    assert(sIsMonBeingMoved);
    if (area == CURSOR_AREA_IN_PARTY) CompactPartySlots();
}

static void childScreen(bool8 naming)
{
    unsigned previous = naming ? routedName : routedSummary;
    if (!naming) { offeredSummary = FALSE; assert(SetMenuTexts_Mon() && offeredSummary); }
    SetPokeStorageTask(naming ? Task_NameBox : Task_ShowMonSummary);
    gTasks[0].func(0);
    gTasks[0].func(0);
    assert(gTasks[0].func == Task_ChangeScreen);
    gTasks[0].func(0);
    assert(sStorage == NULL && liveAllocations == 0);
    assert((naming ? routedName : routedSummary) == previous + 1);
}

static void failReturn(unsigned failure)
{
    unsigned calls = allocationCalls;
    if (failure == 0) failAllocation = allocationCalls + 1;
    else if (failure == 1) failWindows = TRUE;
    else if (failure == 2) failMultiAllocation = TRUE;
    else failMultiWindow = TRUE;
    CB2_ReturnToPokeStorage();
    if (sStorage != NULL)
    {
        driveInit();
        assert(gTasks[0].func == Task_ChangeScreen);
        gTasks[0].func(0);
    }
    assert(sStorage == NULL && liveAllocations == 0 && mainCallback == CB2_ExitPokeStorage);
    static const unsigned expectedCalls[] = {1, 3, 4, 5};
    assert((unsigned)allocationCalls - calls == expectedCalls[failure]); /* rollback allocates nothing */
}

static void partyFailures(void)
{
    for (unsigned failure = 0; failure < 4; failure++)
    {
        setup(3); beginVisit(FALSE, OPTION_MOVE_MONS); driveInit(); remember();
        pickMon(CURSOR_AREA_IN_PARTY, 1);
        SetShiftedMonData(7, 0);
        sCursorArea = CURSOR_AREA_IN_BOX; sCursorPosition = 0;
        childScreen(FALSE);
        trainer.playTimeHours++;
        failReturn(failure);
        verifyRestored();
    }
    puts("PASS party pickup/compaction/swaps/Summary: all four return failure points conserve saved records");
}

static void mailFailures(void)
{
    for (unsigned failure = 0; failure < 4; failure++)
    {
        setup(PARTY_SIZE);
        u16 mail = ITEM_ORANGE_MAIL;
        SetMonData(&gPlayerParty[2], MON_DATA_HELD_ITEM, &mail); gPlayerParty[2].mail = 0;
        SetMonData(&gPlayerParty[4], MON_DATA_HELD_ITEM, &mail); gPlayerParty[4].mail = 1;
        memset(saveBlock.mail, 0x37, sizeof(saveBlock.mail));
        beginVisit(FALSE, OPTION_MOVE_MONS); driveInit(); remember();
        pickMon(CURSOR_AREA_IN_BOX, 0);
        SetShiftedMonData(TOTAL_BOXES_COUNT, 2);
        SetShiftedMonData(TOTAL_BOXES_COUNT, 4);
        assert(sStorage->movingMon.mail == 1);
        sCursorArea = CURSOR_AREA_IN_PARTY; sCursorPosition = 4;
        childScreen(FALSE);
        boxes.boxNames[7][0] = 0xab; boxes.boxWallpapers[7] = 12; boxes.currentBox = 8;
        trainer.playTimeHours++;
        failReturn(failure);
        verifyRestored();
        assert(boxes.boxNames[7][0] == 0xab && boxes.boxWallpapers[7] == 12 && boxes.currentBox == 8);
    }
    puts("PASS full-party mail swap chains restore exact records without relocating mons or reverting box labels");
}

static void itemIcon(u8 index, u8 area, u8 pos)
{
    sStorage->itemIcons[index].active = TRUE;
    sStorage->itemIcons[index].area = area;
    sStorage->itemIcons[index].pos = pos;
}

static void takeItem(void)
{
    sStorage->displayMonItemId = GetMonData(&gPlayerParty[0], MON_DATA_HELD_ITEM);
    itemIcon(0, CURSOR_AREA_IN_PARTY, 0);
    TakeItemFromMon(CURSOR_AREA_IN_PARTY, 0); finishItemAnimations();
    assert(IsMovingItem());
}

static void itemFailures(void)
{
    for (unsigned failure = 0; failure < 4; failure++)
    {
        setup(3);
        u16 item = ITEM_POTION; SetMonData(&gPlayerParty[0], MON_DATA_HELD_ITEM, &item);
        item = ITEM_ANTIDOTE; SetMonData(&gPlayerParty[1], MON_DATA_HELD_ITEM, &item);
        for (unsigned i = 0; i < BAG_ITEMS_COUNT; i++)
        { saveBlock.bagPocket_Items[i].itemId = ITEM_ANTIDOTE; SetBagItemQuantity(&saveBlock.bagPocket_Items[i].quantity, MAX_BAG_ITEM_CAPACITY); }
        beginVisit(FALSE, OPTION_MOVE_ITEMS); driveInit(); remember(); takeItem();
        itemIcon(1, CURSOR_AREA_IN_PARTY, 1);
        SwapItemsWithMon(CURSOR_AREA_IN_PARTY, 1); finishItemAnimations();
        assert(GetMovingItemId() == ITEM_ANTIDOTE);
        SetPokeStorageTask(Task_CloseBoxWhileHoldingItem);
        sStorage->state = 1; yesNoAnswer = 0;
        Task_CloseBoxWhileHoldingItem(0);
        assert(sStorage->state == 2 && IsMovingItem()); /* real AddBagItem refused full bag */
        childScreen(TRUE);
        trainer.playTimeHours++;
        failReturn(failure);
        verifyRestored();
    }
    puts("PASS item TAKE/swap/name with a full bag: failed bag put retains rollback through every init failure");
}

static void closeVisit(void)
{
    sStorage->screenChangeType = SCREEN_CHANGE_EXIT_BOX;
    Task_ChangeScreen(0);
    assert(liveAllocations == 0);
}

static void committedMoves(void)
{
    /* Commit each completion type, then fail a later independent carry. */
    for (unsigned operation = 0; operation < 5; operation++)
    {
        setup(3);
        u16 item = ITEM_POTION; SetMonData(&gPlayerParty[0], MON_DATA_HELD_ITEM, &item);
        beginVisit(FALSE, operation < 3 ? OPTION_MOVE_MONS : OPTION_MOVE_ITEMS); driveInit();
        if (operation < 3)
        {
            pickMon(CURSOR_AREA_IN_BOX, 0);
            childScreen(FALSE); CB2_ReturnToPokeStorage(); driveInit();
            assert(sIsMonBeingMoved && gTasks[0].func == Task_ReshowPokeStorage);
            if (operation == 0) { sCursorArea = CURSOR_AREA_IN_PARTY; sCursorPosition = 3; PlaceMon(); }
            else if (operation == 1) { assert(TryStorePartyMonInBox(6)); }
            else ReleaseMon();
        }
        else
        {
            takeItem();
            childScreen(TRUE); CB2_ReturnToPokeStorage(); driveInit();
            assert(IsMovingItem() && GetMovingItemId() == ITEM_POTION);
            if (operation == 3) { GiveItemToMon(CURSOR_AREA_IN_PARTY, 1); finishItemAnimations(); }
            else
            {
                SetPokeStorageTask(Task_CloseBoxWhileHoldingItem); sStorage->state = 1; yesNoAnswer = 0;
                Task_CloseBoxWhileHoldingItem(0); assert(sStorage->state == 3);
                Task_CloseBoxWhileHoldingItem(0); finishItemAnimations();
            }
        }
#ifdef STORAGE_CARRY_PATCHED
        assert(!sStorageCarry.active);
#endif
        closeVisit();
        /* Keep a deliberately stale count after the completed box-to-party placement. */
        beginVisit(FALSE, OPTION_MOVE_MONS); driveInit(); remember();
        pickMon(CURSOR_AREA_IN_BOX, 1); childScreen(FALSE); trainer.playTimeHours++;
        failReturn(operation % 4); verifyRestored();
    }
    puts("PASS completed placement/deposit/release/give/bag-put survive later rollback; party count is recomputed");

    setup(3);
    for (unsigned i = 0; i < IN_BOX_COUNT; i++) boxes.boxes[6][i] = boxes.boxes[7][1];
    beginVisit(FALSE, OPTION_MOVE_MONS); driveInit(); remember();
    pickMon(CURSOR_AREA_IN_BOX, 0);
    assert(!TryStorePartyMonInBox(6)); /* full destination never commits */
    SetPokeStorageTask(Task_ReleaseMon); sStorage->state = 1; yesNoAnswer = 1;
    Task_ReleaseMon(0); assert(sIsMonBeingMoved); /* explicit release cancellation */
    SetPokeStorageTask(Task_ReleaseMon); sStorage->state = 2;
    Task_ReleaseMon(0); assert(sStorage->state == 8 && sIsMonBeingMoved); /* release refused */
    childScreen(FALSE); trainer.playTimeHours++; failReturn(2); verifyRestored();
    puts("PASS full-box, cancelled release and refused release retain the unfinished transaction");
}

static void noticeAndRekey(void)
{
#ifdef STORAGE_CARRY_PATCHED
    setup(2); beginVisit(FALSE, OPTION_MOVE_MONS); driveInit(); remember();
    pickMon(CURSOR_AREA_IN_PARTY, 1); childScreen(FALSE);
    trainer.playTimeHours++;
    failReturn(1); verifyRestored();
    memset(gTasks, 0, sizeof(gTasks));
    weatherReady = FALSE; printedMessage = NULL; noticePrints = 0;
    Task_PCMainMenu(0);
    assert(printedMessage == sText_StorageCarryRecovered && gTasks[0].tState == STATE_FADE_IN);
    Task_PCMainMenu(0); assert(gTasks[0].tState == STATE_FADE_IN && sStorageCarryRecovered);
    weatherReady = TRUE; Task_PCMainMenu(0);
    assert(gTasks[0].tState == STATE_ERROR_MSG && !sStorageCarryRecovered);
    Task_PCMainMenu(0); assert(gTasks[0].tState == STATE_ERROR_MSG && noticePrints == 1);
    gMain.newKeys = A_BUTTON; Task_PCMainMenu(0); gMain.newKeys = 0;
    assert(gTasks[0].tState == STATE_HANDLE_INPUT && printedMessage == description);
    unsigned calls = allocationCalls;
    failAllocation = allocationCalls + 1;
    CtrStorageCarry_Rollback(); /* a second call neither reallocates nor changes committed state */
    assert((unsigned)allocationCalls == calls && !sStorageCarryRecovered);

    setup(2); beginVisit(FALSE, OPTION_MOVE_MONS); driveInit();
    saveBlock.bagPocket_Items[0].itemId = ITEM_POTION; SetBagItemQuantity(&saveBlock.bagPocket_Items[0].quantity, 3);
    pickMon(CURSOR_AREA_IN_PARTY, 1); childScreen(FALSE);
    trainer.encryptionKey = 0x87654321; /* independent key maintenance must not be rolled back */
    failReturn(0);
    assert(trainer.encryptionKey == 0x87654321 && GetBagItemQuantity(&saveBlock.bagPocket_Items[0].quantity) == 3);
    assert(sizeof(sStorageCarry) <= 40 * 1024);
    printf("PASS recovery notice waits for fade/ack; rollback is idempotent, allocation-free, rekeys quantities; checkpoint=%zu bytes\n", sizeof(sStorageCarry));

    for (unsigned failure = 0; failure < 4; failure++)
    {
        setup(2);
        if (failure == 0) { failAllocation = 1; EnterPokeStorage(OPTION_MOVE_MONS); }
        else
        {
            failWindows = failure == 1; failMultiAllocation = failure == 2; failMultiWindow = failure == 3;
            beginVisit(FALSE, OPTION_MOVE_MONS); driveInit(); gTasks[0].func(0);
        }
        assert(sStorageInitFailed && !sStorageCarryRecovered && liveAllocations == 0);
        memset(gTasks, 0, sizeof(gTasks)); weatherReady = TRUE;
        Task_PCMainMenu(0); assert(printedMessage == sText_StorageMemoryFull);
        Task_PCMainMenu(0); assert(gTasks[0].tState == STATE_ERROR_MSG);
        gMain.newKeys = B_BUTTON; Task_PCMainMenu(0); gMain.newKeys = 0;
        assert(gTasks[0].tState == STATE_HANDLE_INPUT && !sStorageInitFailed);
    }
    puts("PASS first-entry memory failures report try-again without claiming a move was undone");
#endif
}

int main(int argc, char **argv)
{
    const char *test = argc == 2 ? argv[1] : "all";
    (void)Task_PCMainMenu; /* also compiled in the before-fix conservation probe */
#define RUN_CASE(name, fn) if (!strcmp(test, "all") || !strcmp(test, name)) fn()
    RUN_CASE("party", partyFailures);
    RUN_CASE("mail", mailFailures);
    RUN_CASE("items", itemFailures);
    RUN_CASE("commits", committedMoves);
    RUN_CASE("notice", noticeAndRekey);
    assert(liveAllocations == 0);
    return 0;
}
