#include "global.h"
#include <stdbool.h>
#include "android_mystery.h"
#include "ctr_mystery.h"
#include "battle_pyramid.h"
#include "decoration.h"
#include "decoration_inventory.h"
#include "event_data.h"
#include "field_message_box.h"
#include "item.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "save.h"
#include "script.h"
#include "script_pokemon_util.h"
#include "constants/battle_frontier.h"
#include "constants/decorations.h"
#include "constants/flags.h"
#include "constants/items.h"

static bool sSavedSession;

struct IslandEvent
{
    u16 item;
    u16 accessFlag;
    u16 receivedFlag;
    u16 caughtFlag[2];
    u16 defeatedFlag[2];
};

static const struct IslandEvent sIslands[] = {
    {ITEM_EON_TICKET, FLAG_ENABLE_SHIP_SOUTHERN_ISLAND, 0,
     {FLAG_CAUGHT_LATIAS_OR_LATIOS, 0}, {FLAG_DEFEATED_LATIAS_OR_LATIOS, 0}},
    {ITEM_MYSTIC_TICKET, FLAG_ENABLE_SHIP_NAVEL_ROCK, FLAG_RECEIVED_MYSTIC_TICKET,
     {FLAG_CAUGHT_LUGIA, FLAG_CAUGHT_HO_OH}, {FLAG_DEFEATED_LUGIA, FLAG_DEFEATED_HO_OH}},
    {ITEM_AURORA_TICKET, FLAG_ENABLE_SHIP_BIRTH_ISLAND, FLAG_RECEIVED_AURORA_TICKET,
     {FLAG_BATTLED_DEOXYS, 0}, {FLAG_DEFEATED_DEOXYS, 0}},
    {ITEM_OLD_SEA_MAP, FLAG_ENABLE_SHIP_FARAWAY_ISLAND, FLAG_RECEIVED_OLD_SEA_MAP,
     {FLAG_CAUGHT_MEW, 0}, {FLAG_DEFEATED_MEW, 0}},
};

static const u16 sGiftSpecies[] = {SPECIES_JIRACHI, SPECIES_CELEBI};
static const u8 sRegiDolls[] = {DECOR_REGIROCK_DOLL, DECOR_REGICE_DOLL, DECOR_REGISTEEL_DOLL};

void CtrMystery_ResetSession(void)
{
    sSavedSession = false;
}

void CtrMystery_ContinueSession(void)
{
    /* ERROR means the engine recovered one valid slot; Continue accepts it. */
    sSavedSession = gSaveFileStatus == SAVE_STATUS_OK || gSaveFileStatus == SAVE_STATUS_ERROR;
}

void CtrMystery_SavedSession(void)
{
    sSavedSession = true;
}

static int sessionStatus(void)
{
    if (!sSavedSession || !gSaveBlock1Ptr || !gSaveBlock2Ptr || !gPokemonStoragePtr)
        return CTR_MYSTERY_NO_GAME;
    if (gMain.callback1 != CB1_Overworld || gMain.callback2 != CB2_Overworld
        || gMain.inBattle || gLinkTransferringData || gPaletteFade.active
        || ScriptContext_IsEnabled() || ArePlayerFieldControlsLocked()
        || !IsFieldMessageBoxHidden() || gPlayerAvatar.preventStep
        || gPlayerAvatar.tileTransitionState != T_NOT_MOVING
        || CurrentBattlePyramidLocation() != PYRAMID_LOCATION_NONE
        || FlagGet(FLAG_STORING_ITEMS_IN_PYRAMID_BAG))
        return CTR_MYSTERY_BUSY;
    /* Do not touch a bag/decoration view from an unfinished map/save setup. */
    if (gBagPockets[KEYITEMS_POCKET].itemSlots != gSaveBlock1Ptr->bagPocket_KeyItems
        || gBagPockets[KEYITEMS_POCKET].capacity != BAG_KEYITEMS_COUNT
        || gDecorationInventories[DECORCAT_DOLL].items != gSaveBlock1Ptr->decorationDolls
        || gDecorationInventories[DECORCAT_DOLL].size != ARRAY_COUNT(gSaveBlock1Ptr->decorationDolls))
        return CTR_MYSTERY_BUSY;
    return CTR_MYSTERY_AVAILABLE;
}

static bool ownsTicket(const struct IslandEvent *event)
{
    return CheckBagHasItem(event->item, 1) || CheckPCHasItem(event->item, 1);
}

static bool islandCompleted(const struct IslandEvent *event)
{
    for (unsigned i = 0; i < ARRAY_COUNT(event->caughtFlag); ++i)
    {
        if (event->caughtFlag[i] && !FlagGet(event->caughtFlag[i]) && !FlagGet(event->defeatedFlag[i]))
            return false;
    }
    return true;
}

static u16 readSpecies(const struct BoxPokemon *original)
{
    /* The game's getter decrypts in place and can mark a malformed record
     * as a Bad Egg. A menu query must never modify the player's Pokemon. */
    struct BoxPokemon copy = *original;
    return GetBoxMonData(&copy, MON_DATA_SPECIES);
}

static bool giftAlreadyObtained(u16 species)
{
    unsigned dex = SpeciesToNationalPokedexNum(species) - 1;
    /* FLAG_GET_CAUGHT repairs inconsistent imported Dex bits as a side
     * effect. Read the ordinary caught bit conservatively instead. */
    if (gSaveBlock2Ptr->pokedex.owned[dex / 8] & (1u << (dex % 8)))
        return true;
    for (unsigned i = 0; i < PARTY_SIZE; ++i)
        if (readSpecies(&gPlayerParty[i].box) == species)
            return true;
    for (unsigned box = 0; box < TOTAL_BOXES_COUNT; ++box)
        for (unsigned slot = 0; slot < IN_BOX_COUNT; ++slot)
            if (readSpecies(&gPokemonStoragePtr->boxes[box][slot]) == species)
                return true;
    return false;
}

static bool partyHasSpace(void)
{
    if (gPlayerPartyCount >= PARTY_SIZE)
        return false;
    /* GiveMonToPlayer chooses the first empty slot. Require the normal
     * contiguous-party invariant so delivery can never silently use a PC. */
    for (unsigned i = 0; i < PARTY_SIZE; ++i)
        if ((readSpecies(&gPlayerParty[i].box) != SPECIES_NONE) != (i < gPlayerPartyCount))
            return false;
    return true;
}

static unsigned missingDolls(void)
{
    unsigned missing = 0;
    for (unsigned i = 0; i < ARRAY_COUNT(sRegiDolls); ++i)
        missing += !CheckHasDecoration(sRegiDolls[i]);
    return missing;
}

static bool dollsHaveSpace(unsigned missing)
{
    unsigned empty = 0;
    for (unsigned i = 0; i < ARRAY_COUNT(gSaveBlock1Ptr->decorationDolls); ++i)
        empty += gSaveBlock1Ptr->decorationDolls[i] == DECOR_NONE;
    return empty >= missing;
}

static int eventStatus(unsigned id)
{
    int state = sessionStatus();
    if (state != CTR_MYSTERY_AVAILABLE)
        return state;

    if (id < ARRAY_COUNT(sIslands))
    {
        const struct IslandEvent *event = &sIslands[id];
        if (islandCompleted(event))
            return CTR_MYSTERY_COMPLETED;
        if (ownsTicket(event) && FlagGet(event->accessFlag))
            return CTR_MYSTERY_UNLOCKED;
    }
    else if (id == CTR_MYSTERY_JIRACHI || id == CTR_MYSTERY_CELEBI)
    {
        if (giftAlreadyObtained(sGiftSpecies[id - CTR_MYSTERY_JIRACHI]))
            return CTR_MYSTERY_COMPLETED;
    }
    else if (missingDolls() == 0)
        return CTR_MYSTERY_COMPLETED;

    if (!FlagGet(FLAG_SYS_POKEDEX_GET))
        return CTR_MYSTERY_PREREQUISITE;
    if (id < ARRAY_COUNT(sIslands))
    {
        const struct IslandEvent *event = &sIslands[id];
        if (!ownsTicket(event) && !CheckBagHasSpace(event->item, 1))
            return CTR_MYSTERY_BAG_FULL;
    }
    else if (id == CTR_MYSTERY_JIRACHI || id == CTR_MYSTERY_CELEBI)
    {
        if (!partyHasSpace())
            return CTR_MYSTERY_PARTY_FULL;
    }
    else if (!dollsHaveSpace(missingDolls()))
        return CTR_MYSTERY_DECOR_FULL;
    return CTR_MYSTERY_AVAILABLE;
}

unsigned CtrMystery_Query(int *states, unsigned capacity)
{
    if (states)
        for (unsigned i = 0; i < capacity && i < CTR_MYSTERY_EVENT_COUNT; ++i)
            states[i] = eventStatus(i);
    return CTR_MYSTERY_EVENT_COUNT;
}

int CtrMystery_Activate(unsigned id)
{
    if (id >= CTR_MYSTERY_EVENT_COUNT)
        return CTR_MYSTERY_RESULT_INVALID;
    switch (eventStatus(id))
    {
    case CTR_MYSTERY_UNLOCKED:
    case CTR_MYSTERY_COMPLETED: return CTR_MYSTERY_RESULT_ALREADY;
    case CTR_MYSTERY_NO_GAME: return CTR_MYSTERY_RESULT_NO_GAME;
    case CTR_MYSTERY_BUSY: return CTR_MYSTERY_RESULT_BUSY;
    case CTR_MYSTERY_BAG_FULL:
    case CTR_MYSTERY_PARTY_FULL:
    case CTR_MYSTERY_DECOR_FULL: return CTR_MYSTERY_RESULT_NO_SPACE;
    case CTR_MYSTERY_PREREQUISITE: return CTR_MYSTERY_RESULT_FAILED;
    default: break;
    }
    if (id < ARRAY_COUNT(sIslands))
    {
        const struct IslandEvent *event = &sIslands[id];
        if (!ownsTicket(event) && !AddBagItem(event->item, 1))
            return CTR_MYSTERY_RESULT_FAILED;
        FlagSet(event->accessFlag);
        if (event->receivedFlag)
            FlagSet(event->receivedFlag);
        return CTR_MYSTERY_RESULT_ACTIVATED;
    }
    if (id == CTR_MYSTERY_JIRACHI || id == CTR_MYSTERY_CELEBI)
    {
        /* Capacity was checked before any RNG/data mutation. The standard
         * gift path creates/encrypts the Pokemon and sets ordinary Dex bits.
         * This is a player-OT offline gift, not a historical distribution. */
        u8 result = ScriptGiveMon(sGiftSpecies[id - CTR_MYSTERY_JIRACHI], 5, ITEM_NONE, 0, 0, 0);
        return result == MON_GIVEN_TO_PARTY ? CTR_MYSTERY_RESULT_ACTIVATED : CTR_MYSTERY_RESULT_FAILED;
    }
    /* Restore only missing dolls. A placed doll remains in this inventory.
     * There is no native historical claim flag: trading/deleting a doll
     * makes it eligible for restoration again. */
    u8 before[sizeof(gSaveBlock1Ptr->decorationDolls)];
    memcpy(before, gSaveBlock1Ptr->decorationDolls, sizeof(before));
    for (unsigned i = 0; i < ARRAY_COUNT(sRegiDolls); ++i)
    {
        if (!CheckHasDecoration(sRegiDolls[i]) && !DecorationAdd(sRegiDolls[i]))
        {
            memcpy(gSaveBlock1Ptr->decorationDolls, before, sizeof(before));
            return CTR_MYSTERY_RESULT_FAILED;
        }
    }
    return CTR_MYSTERY_RESULT_ACTIVATED;
}
