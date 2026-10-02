/* Links the production QoL bridge to the actual, patched Pokemon and RNG
 * translation units. Only platform/UI and unrelated presentation are faked. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "battle.h"
#include "pokemon.h"
#include "main.h"
#include "random.h"
#include "text.h"
#include "constants/battle_move_effects.h"
#include "constants/moves.h"
#include "constants/region_map_sections.h"
#include "android_qol.h"
#include "android_qol_rules.h"

_Static_assert(sizeof(struct BoxPokemon) == 80, "host fixture must match GBA boxed record");
_Static_assert(sizeof(struct Pokemon) == 100, "host fixture must match GBA party record");

static struct SaveBlock2 trainer;
struct SaveBlock2 *gSaveBlock2Ptr = &trainer;
u32 gBattleTypeFlags;
u8 gBattlersCount;
u8 gAbsentBattlerFlags;
struct BattlePokemon gBattleMons[MAX_BATTLERS_COUNT];
struct BattleScripting gBattleScripting;
struct Main gMain;
const u8 gGameLanguage = LANGUAGE_ENGLISH;
const u8 gGameVersion = VERSION_EMERALD;
const u8 gText_BadEgg[] = {EOS};
const u8 gText_EggNickname[] = {EOS};
/* Full-width fixture name: upstream copies bytes beyond EOS for shorter
 * names, which are unspecified stack padding and irrelevant to game data. */
const u8 gSpeciesNames[NUM_SPECIES + 1][POKEMON_NAME_LENGTH + 1] = {
    [SPECIES_TORCHIC] = {0xce, 0xc9, 0xcc, 0xbd, 0xc2, 0xc3, 0xbd, 0xa2, 0xa3, 0xa4, EOS},
    [SPECIES_POOCHYENA] = {0xca, EOS},
    [SPECIES_WURMPLE] = {0xd1, EOS},
    [SPECIES_UNOWN] = {0xcf, EOS},
};
const u32 gBitTable[32] = {
    1u<<0, 1u<<1, 1u<<2, 1u<<3, 1u<<4, 1u<<5, 1u<<6, 1u<<7,
    1u<<8, 1u<<9, 1u<<10, 1u<<11, 1u<<12, 1u<<13, 1u<<14, 1u<<15,
    1u<<16, 1u<<17, 1u<<18, 1u<<19, 1u<<20, 1u<<21, 1u<<22, 1u<<23,
    1u<<24, 1u<<25, 1u<<26, 1u<<27, 1u<<28, 1u<<29, 1u<<30, 1u<<31,
};

static unsigned multiplier = 1, promptCount;
static bool shared, protect, answer;
unsigned CtrHost_ShinyMultiplier(void) { return multiplier; }
bool CtrHost_SharedExperience(void) { return shared; }
bool CtrHost_ProtectShinies(void) { return protect; }
bool CtrHost_ConfirmShinyFlee(void) { ++promptCount; return answer; }
u8 GetBattlerSide(u8 battler) { return battler & 1; }
u8 GetCurrentRegionMapSectionId(void) { return MAPSEC_ROUTE_101; }
u8 *StringCopy(u8 *dst, const u8 *src) { while ((*dst++ = *src++) != EOS) {} return dst - 1; }
u16 StringLength(const u8 *src) { const u8 *p = src; while (*p != EOS) ++p; return p - src; }

static void validBox(const struct BoxPokemon *mon)
{
    uint32_t words[12];
    memcpy(words, mon->secure.raw, sizeof(words));
    uint16_t sum = 0;
    for (unsigned i = 0; i < 12; ++i)
    {
        uint32_t plain = words[i] ^ mon->personality ^ mon->otId;
        sum = (uint16_t)(sum + (plain & 0xffff) + (plain >> 16));
    }
    assert(sum == mon->checksum && !mon->isBadEgg);
}

static void makeWild(struct Pokemon *mon, u16 species, uint32_t personality)
{
    CtrQol_BeginWildMon(mon);
    CreateMon(mon, species, 5, USE_RANDOM_IVS, TRUE, personality, OT_ID_PLAYER_ID, 0);
    CtrQol_EndWildMon();
}

static void shinyRules(void)
{
    const uint32_t ot = 0x13579bdf;
    unsigned changed = 0, natural = 0;
    for (unsigned value = 0; value < 65536; ++value)
    {
        uint32_t low = 0x8da5;
        uint32_t pid = (((ot >> 16) ^ (ot & 0xffff) ^ low ^ value) << 16) | low;
        for (unsigned m = 1; m <= 64; m <<= 1)
        {
            uint32_t out = CtrQol_BoostPersonality(pid, ot, m, 0);
            if (m == 1 || value < 8 || value >= m * 8) assert(out == pid);
            else {
                assert(out != pid && CtrQol_ShinyValue(ot, out) < 8);
                assert((out & 255) == (pid & 255) && out % 25 == pid % 25);
            }
        }
        uint32_t out = CtrQol_BoostPersonality(pid, ot, 64, 0);
        changed += out != pid;
        natural += value < 8;
    }
    assert(natural == 8 && changed == 504);
    unsigned wurmpleConverted = 0, unownConverted = 0;
    for (unsigned i = 0; i < 4096; ++i)
    {
        uint32_t pid = i * 104729u + 0x400008;
        uint32_t trainerId = (pid ^ (pid >> 16) ^ (8 + i % 504)) & 0xffff;
        assert(CtrQol_ShinyValue(trainerId, pid) == 8 + i % 504);
        uint32_t out = CtrQol_BoostPersonality(pid, trainerId, 64, CTR_QOL_PRESERVE_WURMPLE);
        assert(out == pid || CtrQol_ShinyValue(trainerId, out) < 8);
        wurmpleConverted += out != pid;
        assert(out % 25 == pid % 25 && (out & 255) == (pid & 255));
        assert(((out >> 16) % 10 < 5) == ((pid >> 16) % 10 < 5));
        out = CtrQol_BoostPersonality(pid, trainerId, 64, CTR_QOL_PRESERVE_UNOWN);
        unownConverted += out != pid;
        assert(GET_UNOWN_LETTER(out) == GET_UNOWN_LETTER(pid));
        assert(out == pid || CtrQol_ShinyValue(trainerId, out) < 8);
    }
    assert(wurmpleConverted > 2048 && unownConverted > 0);
    printf("Constrained qualifying fixtures converted: Wurmple %u/4096, Unown %u/4096; others retain original PID\n",
           wurmpleConverted, unownConverted);
    puts("PASS shiny odds boundaries, natural shinies, nature/gender/ability, Wurmple and Unown");
}

static void actualPokemonCreation(void)
{
    const uint32_t ot = 0x1234abcd;
    memcpy(trainer.playerTrainerId, &ot, sizeof(ot));
    memset(trainer.playerName, EOS, sizeof(trainer.playerName));
    struct Pokemon original, disabled, boosted, protectedMon;
    uint32_t pid = (((ot >> 16) ^ (ot & 0xffff) ^ 0x81a5 ^ 12) << 16) | 0x81a5;
    multiplier = 1;
    SeedRng(42);
    CreateMon(&original, SPECIES_TORCHIC, 5, USE_RANDOM_IVS, TRUE, pid, OT_ID_PLAYER_ID, 0);
    uint32_t rng = gRngValue;
    SeedRng(42);
    makeWild(&disabled, SPECIES_TORCHIC, pid);
    assert(gRngValue == rng && !memcmp(&original, &disabled, sizeof(original)));
    validBox(&disabled.box);

    multiplier = 64;
    SeedRng(42);
    makeWild(&boosted, SPECIES_TORCHIC, pid);
    assert(gRngValue == rng); // PID repair never consumes an extra RNG draw.
    assert(GetMonData(&boosted, MON_DATA_PERSONALITY) != pid);
    assert(IsMonShiny(&boosted));
    validBox(&boosted.box);
    const unsigned fields[] = {MON_DATA_SPECIES, MON_DATA_OT_ID, MON_DATA_LEVEL,
        MON_DATA_IVS, MON_DATA_ATK, MON_DATA_DEF, MON_DATA_SPEED, MON_DATA_SPATK,
        MON_DATA_SPDEF, MON_DATA_MAX_HP, MON_DATA_ABILITY_NUM, MON_DATA_MOVE1,
        MON_DATA_MOVE2, MON_DATA_PP1, MON_DATA_PP2};
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        assert(GetMonData(&original, fields[i]) == GetMonData(&boosted, fields[i]));
    assert(GetMonGender(&original) == GetMonGender(&boosted));
    assert(GetNature(&original) == GetNature(&boosted));

    // The same high setting cannot alter gifts/trainers/fixed creations
    // outside the explicitly bracketed ordinary wild creation call.
    SeedRng(42);
    CreateMon(&protectedMon, SPECIES_TORCHIC, 5, USE_RANDOM_IVS, TRUE, pid, OT_ID_PLAYER_ID, 0);
    assert(gRngValue == rng && !memcmp(&original, &protectedMon, sizeof(original)));
    CtrQol_BeginWildMon(&boosted);
    assert(CtrQol_WildPersonality(&protectedMon.box, SPECIES_TORCHIC, pid, OT_ID_PLAYER_ID) == pid);
    assert(CtrQol_WildPersonality(&boosted.box, SPECIES_TORCHIC, pid, OT_ID_RANDOM_NO_SHINY) == pid);
    assert(CtrQol_WildPersonality(&boosted.box, SPECIES_TORCHIC, pid, OT_ID_PRESET) == pid);
    CtrQol_EndWildMon();
    assert(CtrQol_WildPersonality(&boosted.box, SPECIES_TORCHIC, pid, OT_ID_PLAYER_ID) == pid);

    // Use an actual encrypted Pokemon for the EXP bridge checks below.
    gPlayerParty[0] = original;
    gPlayerParty[1] = boosted;
    gPlayerPartyCount = 2;
    puts("PASS actual CreateMon: disabled byte/RNG identity, enabled vanilla shiny encryption and unchanged stats/IVs/moves");
}

static void experienceRules(void)
{
    uint32_t rng = gRngValue;
    for (unsigned flags = 0; flags < 256; ++flags)
    {
        bool enabled = flags & 1, participated = flags & 2, expShare = flags & 4;
        bool occupied = flags & 8, egg = flags & 16, partner = flags & 128;
        unsigned hp = flags & 32 ? 10 : 0, level = flags & 64 ? 100 : 5;
        unsigned value = CtrQol_BenchExperience(101, enabled, participated, expShare, occupied, egg, hp, level, partner);
        assert(value == (enabled && !participated && !expShare && occupied && !egg && hp && level < 100 && !partner ? 50u : 0u));
    }
    assert(CtrQol_BenchExperience(1, true, false, false, true, false, 1, 5, false) == 1);
    shared = false; CtrQol_BeginExperience(101);
    assert(!CtrQol_ExtraExperience(1, false, false));
    shared = true; CtrQol_BeginExperience(101);
    assert(CtrQol_ExtraExperience(1, false, false) == 50);
    assert(!CtrQol_ExtraExperience(1, true, false));
    assert(!CtrQol_ExtraExperience(1, false, true));
    assert(!CtrQol_ExtraExperience(6, false, false));
    shared = false; // Each defeated foe keeps its policy through its messages.
    assert(CtrQol_ExtraExperience(1, false, false) == 50);
    CtrQol_BeginExperience(101);
    assert(!CtrQol_ExtraExperience(1, false, false));
    assert(gRngValue == rng);
    puts("PASS shared EXP eligibility, no existing-reward stacking, per-foe policy and unchanged RNG");
}

static void fleeRules(void)
{
    uint32_t rng = gRngValue;
    gBattlersCount = 4;
    gBattleMons[1].hp = 10;
    gBattleMons[1].otId = 0;
    gBattleMons[1].personality = 0; // vanilla shiny
    protect = false; answer = false;
    assert(CtrQol_ConfirmFlee() && promptCount == 0);
    protect = true;
    const u32 excluded[] = {BATTLE_TYPE_TRAINER, BATTLE_TYPE_LINK, BATTLE_TYPE_RECORDED,
        BATTLE_TYPE_RECORDED_LINK, BATTLE_TYPE_WALLY_TUTORIAL};
    for (unsigned i = 0; i < sizeof(excluded) / sizeof(excluded[0]); ++i) {
        gBattleTypeFlags = excluded[i];
        assert(CtrQol_ConfirmFlee() && !promptCount);
    }
    gBattleTypeFlags = 0;
    gMain.newKeys = gMain.newKeysRaw = gMain.newAndRepeatedKeys = A_BUTTON;
    assert(!CtrQol_ConfirmFlee() && promptCount == 1);
    assert(!gMain.newKeys && !gMain.newKeysRaw && !gMain.newAndRepeatedKeys);
    answer = true;
    assert(CtrQol_ConfirmFlee() && promptCount == 2);
    gAbsentBattlerFlags = 2;
    assert(CtrQol_ConfirmFlee() && promptCount == 2);
    gAbsentBattlerFlags = 0;
    gBattleMons[1].hp = 0;
    assert(CtrQol_ConfirmFlee() && promptCount == 2);
    gBattleMons[3].hp = 10; // second wild opponent must also be protected
    assert(CtrQol_ConfirmFlee() && promptCount == 3);

    answer = false;
    CtrQol_ResetMoveEscape(0);
    assert(!CtrQol_SelectMoveEscape(0, MOVE_TELEPORT) && promptCount == 4);
    answer = true;
    assert(CtrQol_SelectMoveEscape(0, MOVE_TELEPORT) && promptCount == 5);
    assert(CtrQol_ResolveMoveEscape(0) && promptCount == 5); // consume approval once
    answer = false;
    assert(!CtrQol_ResolveMoveEscape(0) && promptCount == 6); // called move, no bypass
    answer = true;
    assert(CtrQol_SelectMoveEscape(0, MOVE_ROAR) && promptCount == 7);
    CtrQol_ResetMoveEscape(0);
    answer = false;
    assert(!CtrQol_ResolveMoveEscape(0) && promptCount == 8);
    assert(CtrQol_SelectMoveEscape(0, MOVE_SCRATCH) && promptCount == 8);
    assert(CtrQol_ResolveMoveEscape(1) && promptCount == 8); // opponent action unchanged
    gBattleTypeFlags = BATTLE_TYPE_PYRAMID;
    assert(!CtrQol_ConfirmFlee() && promptCount == 9); // also covers guaranteed escape items in the Pyramid
    gBattleTypeFlags = 0;
    assert(gRngValue == rng);
    puts("PASS shiny flee confirm/cancel, stale input, exclusions, double wild foes, move latch and called escapes");
}

int main(void)
{
    shinyRules();
    actualPokemonCreation();
    experienceRules();
    fleeRules();
    puts("PASS all QoL engine tests");
    return 0;
}
