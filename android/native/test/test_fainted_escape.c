/* Actual faint/run/loss commands, production shiny guard and host prompt.
 * Fixtures supply only party metadata, item lookup and Android's UI reply. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "battle.h"
#include "battle_controllers.h"
#include "battle_pyramid.h"
#include "main.h"
#include "pokemon.h"
#include "random.h"
#include "constants/abilities.h"
#include "constants/battle_script_commands.h"
#include "constants/hold_effects.h"
#include "constants/items.h"
#include "constants/party_menu.h"
#include "constants/trainers.h"
#include "android_qol.h"
#include "ctr_host.h"

struct BattlePokemon gBattleMons[MAX_BATTLERS_COUNT];
struct ProtectStruct gProtectStructs[MAX_BATTLERS_COUNT];
struct SpecialStatus gSpecialStatuses[MAX_BATTLERS_COUNT];
struct BattleEnigmaBerry gEnigmaBerries[MAX_BATTLERS_COUNT];
struct Pokemon gPlayerParty[PARTY_SIZE], gEnemyParty[PARTY_SIZE];
static struct BattleStruct battle;
struct BattleStruct *gBattleStruct = &battle;
struct Main gMain;
u32 gBattleTypeFlags, gBattleControllerExecFlags, gHitMarker;
u8 gBattlersCount, gAbsentBattlerFlags, gBattlerFainted, gActiveBattler;
u16 gBattlerPartyIndexes[MAX_BATTLERS_COUNT];
u8 gBattleBufferB[MAX_BATTLERS_COUNT][0x200];
struct BattleResults gBattleResults;
u8 gPotentialItemEffectBattler, gCurrentTurnActionNumber, gLastUsedAbility, gBattleOutcome;
u16 gLastUsedItem, gPartnerTrainerId;
const u32 gBitTable[32] = {1, 2, 4, 8, 16, 32};
static const u8 sCommand[16], sFaintEnd[1];
const u8 BattleScript_FaintedMonTryChoose[6] = {0, BS_FAINTED, 0, 0, 0, 0};
const u8 *gBattlescriptCurrInstr;

/* Host pointers are wider than the game's script operands. Their destination
 * is a fixture; the command's branch/side effects execute unchanged. */
#undef T1_READ_PTR
#undef T2_READ_PTR
#define T1_READ_PTR(pointer) sFaintEnd
#define T2_READ_PTR(pointer) sFaintEnd

u8 GetItemHoldEffect(u16 item)
{ return item == ITEM_SMOKE_BALL ? HOLD_EFFECT_CAN_ALWAYS_RUN : 0; }
u8 CurrentBattlePyramidLocation(void) { return PYRAMID_LOCATION_NONE; }
u8 GetPyramidRunMultiplier(void) { return 128; }
u8 GetBattlerSide(u8 battler) { return battler & 1; }
u8 GetBattlerPosition(u8 battler) { return battler; }
u8 GetBattlerAtPosition(u8 position) { return position; }
u8 GetBattlerForBattleScript(u8 which)
{ assert(which == BS_FAINTED); return gBattlerFainted; }
bool8 HasNoMonsToSwitch(u8 battler, u8 first, u8 second)
{ assert(!battler && first == PARTY_SIZE && second == PARTY_SIZE); return !gPlayerParty[1].hp; }
static unsigned replacementRequests;
static void ChooseMonToSendOut(u8 slot) { (void)slot; assert(!"unexpected link replacement"); }
void BtlController_EmitChoosePokemon(u8 buffer, u8 kind, u8 slot, u8 ability, u8 *data)
{
    (void)slot; (void)data;
    assert(buffer == B_COMM_TO_CONTROLLER && kind == PARTY_ACTION_SEND_OUT);
    assert(!gActiveBattler && ability == ABILITY_NONE);
    ++replacementRequests;
}
void BtlController_EmitCantSwitch(u8 buffer) { (void)buffer; assert(!"unexpected link replacement"); }
void BtlController_EmitLinkStandbyMsg(u8 buffer, u8 mode, bool32 record)
{ assert(buffer == B_COMM_TO_CONTROLLER && mode == LINK_STANDBY_MSG_ONLY && !record); }
void MarkBattlerForControllerExec(u8 battler) { gBattleControllerExecFlags |= 1u << battler; }
u32 GetMonData2(struct Pokemon *mon, s32 field)
{
    switch (field)
    {
    case MON_DATA_SPECIES: return mon->box.hasSpecies ? SPECIES_TORCHIC : SPECIES_NONE;
    case MON_DATA_IS_EGG: return mon->box.isEgg;
    case MON_DATA_HP: return mon->hp;
    default: assert(!"unexpected party getter"); return 0;
    }
}

enum UiReply { STAY, LEAVE, MISSING_UI, PAUSE_DURING_PROMPT };
static enum UiReply reply;
static unsigned prompts;
bool CtrHost_ShowShinyFleePrompt(uint32_t request)
{
    ++prompts;
    assert(CtrHost_IsShinyFleePending(request));
    if (reply == MISSING_UI) return false;
    if (reply == PAUSE_DURING_PROMPT) CtrHost_SetState(CTR_HOST_PAUSED);
    else CtrHost_AnswerShinyFlee(request, reply == LEAVE);
    return true;
}

/* Extracted from the generated game by run_qol_tests.py. Includes the original
 * command from the pinned pret tree for default-off differential checks. */
#include "flee_commands.inc"

static void prepare(bool enabled, enum UiReply value, unsigned variant, unsigned seed)
{
    memset(&battle, 0, sizeof(battle));
    memset(&gMain, 0, sizeof(gMain));
    memset(gBattleMons, 0, sizeof(gBattleMons));
    memset(gProtectStructs, 0, sizeof(gProtectStructs));
    memset(gSpecialStatuses, 0, sizeof(gSpecialStatuses));
    memset(&gBattleResults, 0, sizeof(gBattleResults));
    memset(gPlayerParty, 0, sizeof(gPlayerParty));
    memset(gEnemyParty, 0, sizeof(gEnemyParty));
    gBattlersCount = 2;
    gBattleTypeFlags = gBattleControllerExecFlags = gAbsentBattlerFlags = 0;
    gHitMarker = HITMARKER_PLAYER_FAINTED;
    gBattlerFainted = gBattleOutcome = gCurrentTurnActionNumber = 0;
    gLastUsedAbility = gLastUsedItem = gPotentialItemEffectBattler = 0;
    gPlayerParty[0].box.hasSpecies = gPlayerParty[1].box.hasSpecies = 1;
    gPlayerParty[1].hp = 10; // a legal replacement remains
    gEnemyParty[0].box.hasSpecies = 1;
    gEnemyParty[0].hp = 10;
    gBattleMons[0].hp = 0;
    gBattleMons[0].speed = variant == 1 ? 1 : 100;
    gBattleMons[0].item = variant == 2 ? ITEM_SMOKE_BALL : ITEM_NONE;
    gBattleMons[0].ability = variant == 3 ? ABILITY_RUN_AWAY : ABILITY_NONE;
    gBattleMons[1].hp = gBattleMons[1].speed = 10;
    // personality=otId=0 is a real Gen III shiny
    gBattlescriptCurrInstr = sCommand;
    gMain.newKeys = gMain.newKeysRaw = gMain.newAndRepeatedKeys = A_BUTTON;
    reply = value;
    prompts = 0;
    replacementRequests = 0;
    SeedRng(seed);
    CtrHost_SetState(CTR_HOST_RUNNING);
    CtrHost_SetGameplayOptions(1, 1, false, false, enabled);
}

typedef struct {
    u32 rng;
    const u8 *script;
    u16 item;
    u8 outcome, tries, turn, ability, effectBattler, fleeType;
} RunResult;

static RunResult runResult(void)
{
    return (RunResult){gRngValue, gBattlescriptCurrInstr, gLastUsedItem,
        gBattleOutcome, battle.runTries, gCurrentTurnActionNumber,
        gLastUsedAbility, gPotentialItemEffectBattler, gProtectStructs[0].fleeType};
}

static void sameResult(RunResult before, RunResult after)
{
    assert(before.rng == after.rng && before.script == after.script);
    assert(before.item == after.item && before.outcome == after.outcome);
    assert(before.tries == after.tries && before.turn == after.turn);
    assert(before.ability == after.ability && before.effectBattler == after.effectBattler);
    assert(before.fleeType == after.fleeType);
}

int main(void)
{
    for (unsigned variant = 0; variant < 4; ++variant)
        for (unsigned seed = 0; seed < 32; ++seed)
        {
            prepare(false, STAY, variant, seed);
            Cmd_jumpifplayerran_original();
            RunResult original = runResult();
            prepare(false, STAY, variant, seed);
            Cmd_jumpifplayerran();
            sameResult(original, runResult());
            assert(prompts == 0 && gMain.newKeys == A_BUTTON);

            prepare(true, LEAVE, variant, seed);
            Cmd_jumpifplayerran();
            sameResult(original, runResult());
            assert(prompts == 1 && !gMain.newKeys);
        }

    const enum UiReply cancellations[] = {STAY, MISSING_UI, PAUSE_DURING_PROMPT};
    for (unsigned i = 0; i < ARRAY_COUNT(cancellations); ++i)
        for (unsigned variant = 0; variant < 4; ++variant)
        {
            prepare(true, cancellations[i], variant, 42);
            Cmd_jumpifplayerran();
            assert(prompts == 1 && !gBattleOutcome && !battle.runTries);
            assert(gRngValue == 42 && !gCurrentTurnActionNumber);
            assert(!gLastUsedAbility && !gLastUsedItem && !gProtectStructs[0].fleeType);
            assert(gBattlescriptCurrInstr == BattleScript_FaintedMonTryChoose);
            assert(!gMain.newKeys && !gMain.newKeysRaw && !gMain.newAndRepeatedKeys);
            Cmd_openpartyscreen();
            assert(replacementRequests == 1 && gBattleControllerExecFlags == 3);
            assert(gBattlescriptCurrInstr == BattleScript_FaintedMonTryChoose + 6);
            assert(gBattleResults.playerSwitchesCounter == 1 && !gBattleOutcome);
        }

    prepare(true, STAY, 0, 42);
    gBattleMons[1].personality = 0x1000; // ordinary opponent
    Cmd_jumpifplayerran();
    assert(prompts == 0 && gBattleOutcome == B_OUTCOME_RAN);

    /* The actual battle script checks team loss before offering Yes/No.
     * The runner checks that ordering and the replacement script's entry. */
    prepare(true, STAY, 0, 42);
    Cmd_checkteamslost();
    assert(!gBattleOutcome && !prompts); // living replacement, may offer No
    gPlayerParty[1].hp = 0;
    Cmd_checkteamslost();
    assert(gBattleOutcome == B_OUTCOME_LOST && !prompts); // mandatory loss, no flee prompt

    puts("PASS fainted-menu escape: real command/run/loss functions + host prompt; disabled/Leave match original across128 routes, Stay/missing UI/pause preserve RNG/counters and select replacement, ordinary opponents and all-party loss unchanged");
    return 0;
}
