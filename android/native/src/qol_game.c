#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "main.h"
#include "pokemon.h"
#include "constants/battle_move_effects.h"
#include "constants/moves.h"
#include "android_qol.h"
#include "android_qol_rules.h"
#include "ctr_host.h"

static struct BoxPokemon *sWildMon;
static unsigned sWildMultiplier;
static unsigned sExperienceBase;
static bool sExperienceEnabled;
static bool sMoveEscapeApproved[MAX_BATTLERS_COUNT];

void CtrQol_BeginWildMon(struct Pokemon *mon)
{
    sWildMultiplier = CtrHost_ShinyMultiplier();
    sWildMon = sWildMultiplier > 1 && mon ? &mon->box : NULL;
}

void CtrQol_EndWildMon(void)
{
    sWildMon = NULL;
    sWildMultiplier = 1;
}

uint32_t CtrQol_WildPersonality(struct BoxPokemon *mon, uint16_t species,
                              uint32_t personality, unsigned otIdType)
{
    if (!sWildMon || mon != sWildMon || otIdType != OT_ID_PLAYER_ID)
        return personality;
    const u8 *id = gSaveBlock2Ptr->playerTrainerId;
    uint32_t trainer = (uint32_t)id[0] | ((uint32_t)id[1] << 8)
                     | ((uint32_t)id[2] << 16) | ((uint32_t)id[3] << 24);
    unsigned constraints = species == SPECIES_UNOWN ? CTR_QOL_PRESERVE_UNOWN : 0;
    if (species == SPECIES_WURMPLE) constraints |= CTR_QOL_PRESERVE_WURMPLE;
    return CtrQol_BoostPersonality(personality, trainer, sWildMultiplier, constraints);
}

void CtrQol_BeginExperience(unsigned baseExperience)
{
    /* One defeated opponent uses one policy even if Settings changes while
     * its reward/level-up messages are being shown. */
    sExperienceEnabled = CtrHost_SharedExperience();
    sExperienceBase = baseExperience;
}

unsigned CtrQol_ExtraExperience(unsigned partyIndex, bool participated, bool heldExpShare)
{
    if (!sExperienceEnabled || partyIndex >= gPlayerPartyCount || participated || heldExpShare)
        return 0;
    struct Pokemon *mon = &gPlayerParty[partyIndex];
    return CtrQol_BenchExperience(sExperienceBase, true, participated, heldExpShare,
        GetMonData(mon, MON_DATA_SPECIES) != SPECIES_NONE,
        GetMonData(mon, MON_DATA_IS_EGG) != 0, GetMonData(mon, MON_DATA_HP),
        GetMonData(mon, MON_DATA_LEVEL),
        (gBattleTypeFlags & BATTLE_TYPE_INGAME_PARTNER) && partyIndex >= PARTY_SIZE / 2);
}

bool CtrQol_ConfirmFlee(void)
{
    if (!CtrHost_ProtectShinies()
        || (gBattleTypeFlags & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK | BATTLE_TYPE_RECORDED
            | BATTLE_TYPE_RECORDED_LINK | BATTLE_TYPE_WALLY_TUTORIAL)))
        return true;
    for (unsigned i = 0; i < gBattlersCount && i < MAX_BATTLERS_COUNT; ++i)
    {
        if (GetBattlerSide(i) != B_SIDE_OPPONENT || !gBattleMons[i].hp
            || (gAbsentBattlerFlags & (1u << i)))
            continue;
        if (CtrQol_ShinyValue(gBattleMons[i].otId, gBattleMons[i].personality) < SHINY_ODDS)
        {
            bool allow = CtrHost_ConfirmShinyFlee();
            /* Dismissal/confirmation keys belong to Android's prompt, not
             * another action in the current game input frame. */
            gMain.newKeys = gMain.newKeysRaw = gMain.newAndRepeatedKeys = 0;
            return allow;
        }
    }
    return true;
}

void CtrQol_ResetMoveEscape(unsigned battler)
{
    if (battler < MAX_BATTLERS_COUNT) sMoveEscapeApproved[battler] = false;
}

bool CtrQol_SelectMoveEscape(unsigned battler, unsigned move)
{
    if (battler >= MAX_BATTLERS_COUNT || GetBattlerSide(battler) != B_SIDE_PLAYER)
        return true;
    if (move >= MOVES_COUNT || (gBattleMoves[move].effect != EFFECT_TELEPORT && gBattleMoves[move].effect != EFFECT_ROAR))
        return true;
    bool allow = CtrQol_ConfirmFlee();
    sMoveEscapeApproved[battler] = allow;
    return allow;
}

bool CtrQol_ResolveMoveEscape(unsigned battler)
{
    if (battler >= MAX_BATTLERS_COUNT || GetBattlerSide(battler) != B_SIDE_PLAYER)
        return true;
    bool approved = sMoveEscapeApproved[battler];
    sMoveEscapeApproved[battler] = false;
    /* Also catches an escape move called by Metronome/Sleep Talk, before
     * the engine removes a battler. A declined called move fails normally. */
    return approved || CtrQol_ConfirmFlee();
}
