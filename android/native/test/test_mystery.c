#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "android_mystery.h"
#include "ctr_mystery.h"
#include "battle.h"
#include "battle_pyramid.h"
#include "decoration.h"
#include "decoration_inventory.h"
#include "event_data.h"
#include "item.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "random.h"
#include "save.h"
#include "text.h"
#include "constants/battle_frontier.h"
#include "constants/decorations.h"
#include "constants/flags.h"
#include "constants/region_map_sections.h"

static struct SaveBlock1 world;
static struct SaveBlock2 trainer;
static struct PokemonStorage storage;
struct SaveBlock1 *gSaveBlock1Ptr = &world;
struct SaveBlock2 *gSaveBlock2Ptr = &trainer;
struct PokemonStorage *gPokemonStoragePtr = &storage;
struct Main gMain;
struct PlayerAvatar gPlayerAvatar;
struct PaletteFadeControl gPaletteFade;
bool8 gLinkTransferringData;
u16 gSaveFileStatus;
u16 *const gSpecialVars[24] = {0};
u32 gBattleTypeFlags;
u8 gBattlersCount, gAbsentBattlerFlags;
struct BattlePokemon gBattleMons[MAX_BATTLERS_COUNT];
struct BattleScripting gBattleScripting;
const u8 gGameLanguage = LANGUAGE_ENGLISH;
const u8 gGameVersion = VERSION_EMERALD;
const u8 gText_BadEgg[] = {EOS};
const u8 gText_EggNickname[] = {EOS};
const u8 gSpeciesNames[NUM_SPECIES + 1][POKEMON_NAME_LENGTH + 1] = {
    [SPECIES_TORCHIC] = {0xce,0xc9,0xcc,0xbd,0xc2,0xc3,0xbd,0xa2,0xa3,0xa4,EOS},
    [SPECIES_JIRACHI] = {0xc4,0xc3,0xcc,0xbb,0xbd,0xc2,0xc3,0xa2,0xa3,0xa4,EOS},
    [SPECIES_CELEBI] = {0xbd,0xbf,0xc6,0xbf,0xbc,0xc3,0xa1,0xa2,0xa3,0xa4,EOS},
};
const u32 gBitTable[32] = {
    1u<<0,1u<<1,1u<<2,1u<<3,1u<<4,1u<<5,1u<<6,1u<<7,
    1u<<8,1u<<9,1u<<10,1u<<11,1u<<12,1u<<13,1u<<14,1u<<15,
    1u<<16,1u<<17,1u<<18,1u<<19,1u<<20,1u<<21,1u<<22,1u<<23,
    1u<<24,1u<<25,1u<<26,1u<<27,1u<<28,1u<<29,1u<<30,1u<<31,
};
const struct Item gItems[ITEMS_COUNT] = {
    [ITEM_EON_TICKET] = {.pocket=POCKET_KEY_ITEMS},
    [ITEM_MYSTIC_TICKET] = {.pocket=POCKET_KEY_ITEMS},
    [ITEM_AURORA_TICKET] = {.pocket=POCKET_KEY_ITEMS},
    [ITEM_OLD_SEA_MAP] = {.pocket=POCKET_KEY_ITEMS},
    [ITEM_MACH_BIKE] = {.pocket=POCKET_KEY_ITEMS},
};
const struct Decoration gDecorations[DECOR_REGISTEEL_DOLL + 1] = {
    [DECOR_REGIROCK_DOLL] = {.category=DECORCAT_DOLL},
    [DECOR_REGICE_DOLL] = {.category=DECORCAT_DOLL},
    [DECOR_REGISTEEL_DOLL] = {.category=DECORCAT_DOLL},
    [DECOR_PIKACHU_DOLL] = {.category=DECORCAT_DOLL},
};

static bool script, locked, message, rejectBag;
static u8 pyramid;
static unsigned decorCalls, failDecorCall;
void CB1_Overworld(void) {}
void CB2_Overworld(void) {}
bool8 ScriptContext_IsEnabled(void) { return script; }
bool8 ArePlayerFieldControlsLocked(void) { return locked; }
bool8 IsFieldMessageBoxHidden(void) { return !message; }
u8 CurrentBattlePyramidLocation(void) { return pyramid; }
void InitDecorationContextItems(void) {}
void *AllocZeroed(u32 size) { void *p=calloc(1,size); assert(p); return p; }
void *Alloc(u32 size) { void *p=malloc(size); assert(p); return p; }
void Free(void *p) { free(p); }
u8 GetCurrentRegionMapSectionId(void) { return MAPSEC_ROUTE_101; }
u8 *StringCopy(u8 *d,const u8 *s) { while ((*d++=*s++)!=EOS) {} return d-1; }
u16 StringLength(const u8 *s) { const u8 *p=s; while (*p!=EOS) ++p; return p-s; }
/* These paths must never be used by a preflighted event activation. */
u8 StorageGetCurrentBox(void) { assert(!"unexpected PC gift path"); return 0; }
void SetPCBoxToSendMon(u8 box) { (void)box; assert(!"unexpected PC gift path"); }
u8 GetPCBoxToSendMon(void) { assert(!"unexpected PC gift path"); return 0; }
struct BoxPokemon *GetBoxedMonPtr(u8 box,u8 slot) { return &storage.boxes[box][slot]; }
bool8 __real_AddBagItem(u16,u16);
bool8 __wrap_AddBagItem(u16 item,u16 count) { return rejectBag?FALSE:__real_AddBagItem(item,count); }
bool8 __real_DecorationAdd(u8);
bool8 __wrap_DecorationAdd(u8 item) { return ++decorCalls==failDecorCall?FALSE:__real_DecorationAdd(item); }

struct Snapshot {
    struct SaveBlock1 world;
    struct SaveBlock2 trainer;
    struct PokemonStorage storage;
    struct Pokemon party[PARTY_SIZE];
    u32 rng,rng2;
    u8 partyCount;
};
static struct Snapshot before;
static void takeSnapshot(void) {
    before.world=world; before.trainer=trainer; before.storage=storage;
    memcpy(before.party,gPlayerParty,sizeof(before.party));
    before.rng=gRngValue; before.rng2=gRng2Value;before.partyCount=gPlayerPartyCount;
}
static void unchanged(void) {
    assert(!memcmp(&before.world,&world,sizeof(world)));
    assert(!memcmp(&before.trainer,&trainer,sizeof(trainer)));
    assert(!memcmp(&before.storage,&storage,sizeof(storage)));
    assert(!memcmp(before.party,gPlayerParty,sizeof(before.party)));
    assert(before.rng==gRngValue && before.rng2==gRng2Value && before.partyCount==gPlayerPartyCount);
}
static int status(unsigned id) {
    int states[CTR_MYSTERY_EVENT_COUNT];
    assert(CtrMystery_Query(states,ARRAY_COUNT(states))==CTR_MYSTERY_EVENT_COUNT);
    return states[id];
}
static void reset(void) {
    memset(&world,0,sizeof(world));memset(&trainer,0,sizeof(trainer));memset(&storage,0,sizeof(storage));
    memset(&gMain,0,sizeof(gMain));memset(&gPlayerAvatar,0,sizeof(gPlayerAvatar));
    memset(&gPaletteFade,0,sizeof(gPaletteFade));memset(gPlayerParty,0,sizeof(struct Pokemon)*PARTY_SIZE);
    gSaveBlock1Ptr=&world;gSaveBlock2Ptr=&trainer;gPokemonStoragePtr=&storage;
    trainer.encryptionKey=0xa5bb2233;memset(trainer.playerName,0xbb,PLAYER_NAME_LENGTH);trainer.playerName[PLAYER_NAME_LENGTH]=EOS;
    const u32 id=0x13577fcd;memcpy(trainer.playerTrainerId,&id,sizeof(id));
    InitEventData();SetBagItemsPointers();ClearBag();SetDecorationInventoriesPointers();
    gMain.callback1=CB1_Overworld;gMain.callback2=CB2_Overworld;
    script=locked=message=rejectBag=false;pyramid=PYRAMID_LOCATION_NONE;
    gLinkTransferringData=false;decorCalls=failDecorCall=0;gSaveFileStatus=SAVE_STATUS_OK;
    FlagSet(FLAG_SYS_POKEDEX_GET);gPlayerPartyCount=1;
    CreateMon(&gPlayerParty[0],SPECIES_TORCHIC,5,0,TRUE,0x132357,OT_ID_PLAYER_ID,0);
    CtrMystery_ContinueSession();SeedRng(0x1234);gRng2Value=0x5678;
}
static void assertBlocked(int expected,int result) {
    takeSnapshot();
    for(unsigned i=0;i<CTR_MYSTERY_EVENT_COUNT;i++) {
        assert(status(i)==expected);assert(CtrMystery_Activate(i)==result);
    }
    unchanged();
}
static void sessions(void) {
    reset();CtrMystery_ResetSession();assertBlocked(CTR_MYSTERY_NO_GAME,CTR_MYSTERY_RESULT_NO_GAME);
    gSaveFileStatus=SAVE_STATUS_EMPTY;CtrMystery_ContinueSession();assertBlocked(CTR_MYSTERY_NO_GAME,CTR_MYSTERY_RESULT_NO_GAME);
    CtrMystery_SavedSession();assert(status(0)==CTR_MYSTERY_AVAILABLE);
    CtrMystery_ResetSession();gSaveFileStatus=SAVE_STATUS_ERROR;CtrMystery_ContinueSession();assert(status(0)==CTR_MYSTERY_AVAILABLE);
    reset();FlagClear(FLAG_SYS_POKEDEX_GET);assertBlocked(CTR_MYSTERY_PREREQUISITE,CTR_MYSTERY_RESULT_FAILED);
#define BUSY(field,value) do {reset();(field)=(value);assertBlocked(CTR_MYSTERY_BUSY,CTR_MYSTERY_RESULT_BUSY);} while(0)
    BUSY(gMain.callback1,NULL);BUSY(gMain.callback2,NULL);BUSY(gMain.inBattle,1);
    BUSY(gLinkTransferringData,true);BUSY(gPaletteFade.active,1);BUSY(script,true);BUSY(locked,true);BUSY(message,true);
    BUSY(gPlayerAvatar.preventStep,1);BUSY(gPlayerAvatar.tileTransitionState,T_TILE_TRANSITION);BUSY(pyramid,PYRAMID_LOCATION_FLOOR);
    reset();FlagSet(FLAG_STORING_ITEMS_IN_PYRAMID_BAG);assertBlocked(CTR_MYSTERY_BUSY,CTR_MYSTERY_RESULT_BUSY);
    reset();gBagPockets[KEYITEMS_POCKET].itemSlots=NULL;assertBlocked(CTR_MYSTERY_BUSY,CTR_MYSTERY_RESULT_BUSY);
    reset();gSaveBlock1Ptr=NULL;assertBlocked(CTR_MYSTERY_NO_GAME,CTR_MYSTERY_RESULT_NO_GAME);
    reset();int out[9];for(unsigned i=0;i<9;i++)out[i]=-91;
    assert(CtrMystery_Query(out,2)==7 && out[2]==-91);
    assert(CtrMystery_Query(out,9)==7 && out[7]==-91 && out[8]==-91);
    assert(CtrMystery_Query(NULL,0xffffffffu)==7);
    takeSnapshot();assert(CtrMystery_Activate(0xffffffffu)==CTR_MYSTERY_RESULT_INVALID);unchanged();
    puts("PASS valid session lifecycle, no-game/prerequisite/unsafe gates, bounded snapshot and invalid IDs");
}
static const u16 tickets[]={ITEM_EON_TICKET,ITEM_MYSTIC_TICKET,ITEM_AURORA_TICKET,ITEM_OLD_SEA_MAP};
static const u16 access[]={FLAG_ENABLE_SHIP_SOUTHERN_ISLAND,FLAG_ENABLE_SHIP_NAVEL_ROCK,FLAG_ENABLE_SHIP_BIRTH_ISLAND,FLAG_ENABLE_SHIP_FARAWAY_ISLAND};
static const u16 receipt[]={0,FLAG_RECEIVED_MYSTIC_TICKET,FLAG_RECEIVED_AURORA_TICKET,FLAG_RECEIVED_OLD_SEA_MAP};
static void narrowTicketDelta(unsigned id) {
    const u8 *a=(const u8*)&before.world,*b=(const u8*)&world;
    for(size_t i=0;i<sizeof(world);i++) {
        bool bag=i>=offsetof(struct SaveBlock1,bagPocket_KeyItems) && i<offsetof(struct SaveBlock1,bagPocket_KeyItems)+sizeof(world.bagPocket_KeyItems);
        bool flag=i==offsetof(struct SaveBlock1,flags)+access[id]/8 || (receipt[id] && i==offsetof(struct SaveBlock1,flags)+receipt[id]/8);
        if(!bag&&!flag)assert(a[i]==b[i]);
        if(flag) {u8 allowed=0;if(i==offsetof(struct SaveBlock1,flags)+access[id]/8)allowed|=1u<<(access[id]%8);if(receipt[id]&&i==offsetof(struct SaveBlock1,flags)+receipt[id]/8)allowed|=1u<<(receipt[id]%8);assert(((a[i]^b[i])&~allowed)==0);}
    }
    assert(!memcmp(&before.trainer,&trainer,sizeof(trainer)));assert(!memcmp(before.party,gPlayerParty,sizeof(before.party)));
    assert(!memcmp(&before.storage,&storage,sizeof(storage)));assert(before.rng==gRngValue && before.rng2==gRng2Value);
}
static void islands(void) {
    for(unsigned id=0;id<4;id++) {
        reset();takeSnapshot();assert(status(id)==CTR_MYSTERY_AVAILABLE);unchanged();
        assert(CtrMystery_Activate(id)==CTR_MYSTERY_RESULT_ACTIVATED);narrowTicketDelta(id);
        assert(CheckBagHasItem(tickets[id],1) && !CheckBagHasItem(tickets[id],2) && FlagGet(access[id]));
        if(receipt[id])assert(FlagGet(receipt[id]));
        assert(!FlagGet(FLAG_SYS_GAME_CLEAR));assert(status(id)==CTR_MYSTERY_UNLOCKED);
        takeSnapshot();assert(CtrMystery_Activate(id)==CTR_MYSTERY_RESULT_ALREADY);unchanged();
        reset();assert(AddBagItem(tickets[id],1));assert(status(id)==CTR_MYSTERY_AVAILABLE);
        assert(CtrMystery_Activate(id)==CTR_MYSTERY_RESULT_ACTIVATED);assert(!CheckBagHasItem(tickets[id],2));
        reset();world.pcItems[0].itemId=tickets[id];world.pcItems[0].quantity=1;
        assert(CtrMystery_Activate(id)==CTR_MYSTERY_RESULT_ACTIVATED);assert(!CheckBagHasItem(tickets[id],1));assert(CheckPCHasItem(tickets[id],1));
        assert(status(id)==CTR_MYSTERY_UNLOCKED);
        reset();FlagSet(access[id]);assert(status(id)==CTR_MYSTERY_AVAILABLE);assert(CtrMystery_Activate(id)==CTR_MYSTERY_RESULT_ACTIVATED);
        reset();for(unsigned j=0;j<BAG_KEYITEMS_COUNT;j++){world.bagPocket_KeyItems[j].itemId=ITEM_MACH_BIKE;world.bagPocket_KeyItems[j].quantity=1^trainer.encryptionKey;}
        takeSnapshot();assert(status(id)==CTR_MYSTERY_BAG_FULL);assert(CtrMystery_Activate(id)==CTR_MYSTERY_RESULT_NO_SPACE);unchanged();
        reset();rejectBag=true;takeSnapshot();assert(CtrMystery_Activate(id)==CTR_MYSTERY_RESULT_FAILED);unchanged();
    }
    const u16 completed[][2]={{FLAG_CAUGHT_LATIAS_OR_LATIOS,FLAG_DEFEATED_LATIAS_OR_LATIOS},{FLAG_CAUGHT_LUGIA,FLAG_DEFEATED_LUGIA},{FLAG_BATTLED_DEOXYS,FLAG_DEFEATED_DEOXYS},{FLAG_CAUGHT_MEW,FLAG_DEFEATED_MEW}};
    for(unsigned id=0;id<4;id++)for(unsigned won=0;won<2;won++){
        reset();FlagSet(completed[id][won]);if(id==1)FlagSet(won?FLAG_DEFEATED_HO_OH:FLAG_CAUGHT_HO_OH);
        takeSnapshot();assert(status(id)==CTR_MYSTERY_COMPLETED);assert(CtrMystery_Activate(id)==CTR_MYSTERY_RESULT_ALREADY);unchanged();
    }
    reset();FlagSet(FLAG_CAUGHT_LUGIA);assert(CtrMystery_Activate(1)==CTR_MYSTERY_RESULT_ACTIVATED);assert(FlagGet(FLAG_CAUGHT_LUGIA));assert(!FlagGet(FLAG_CAUGHT_HO_OH));assert(status(1)==CTR_MYSTERY_UNLOCKED);
    puts("PASS four island grants, encrypted inventory, partial repairs, PC ownership, completion, no duplicates and atomic failures");
}
static void validBox(const struct BoxPokemon *mon) {
    u16 sum=0;for(unsigned i=0;i<12;i++){u32 plain=mon->secure.raw[i]^mon->personality^mon->otId;sum+=(u16)plain+(u16)(plain>>16);}
    assert(sum==mon->checksum && !mon->isBadEgg);
}
static void gifts(void) {
    const u16 species[]={SPECIES_JIRACHI,SPECIES_CELEBI};
    for(unsigned n=0;n<2;n++) {
        unsigned id=CTR_MYSTERY_JIRACHI+n;reset();
        struct Pokemon expected;SeedRng(0x1281);CreateMon(&expected,species[n],5,USE_RANDOM_IVS,FALSE,0,OT_ID_PLAYER_ID,0);SeedRng(0x1281);
        assert(CtrMystery_Activate(id)==CTR_MYSTERY_RESULT_ACTIVATED);assert(gPlayerPartyCount==2);
        struct Pokemon *gift=&gPlayerParty[1];validBox(&gift->box);
        assert(GetMonData(gift,MON_DATA_SPECIES)==species[n] && GetMonData(gift,MON_DATA_LEVEL)==5);
        assert(GetMonData(gift,MON_DATA_HELD_ITEM)==ITEM_NONE && GetMonData(gift,MON_DATA_OT_ID)==0x13577fcd);
        assert(!memcmp(gift,&expected,sizeof(expected)));assert(GetSetPokedexFlag(SpeciesToNationalPokedexNum(species[n]),FLAG_GET_CAUGHT));
        takeSnapshot();assert(status(id)==CTR_MYSTERY_COMPLETED);assert(CtrMystery_Activate(id)==CTR_MYSTERY_RESULT_ALREADY);unchanged();
        memset(gift,0,sizeof(*gift));gPlayerPartyCount=1;takeSnapshot();assert(CtrMystery_Activate(id)==CTR_MYSTERY_RESULT_ALREADY);unchanged();
        reset();gPlayerPartyCount=PARTY_SIZE;for(unsigned j=1;j<PARTY_SIZE;j++)gPlayerParty[j]=gPlayerParty[0];
        takeSnapshot();assert(status(id)==CTR_MYSTERY_PARTY_FULL);assert(CtrMystery_Activate(id)==CTR_MYSTERY_RESULT_NO_SPACE);unchanged();
        reset();storage.boxes[13][29]=expected.box;takeSnapshot();assert(status(id)==CTR_MYSTERY_COMPLETED);unchanged();
        reset();unsigned dex=SpeciesToNationalPokedexNum(species[n])-1;trainer.pokedex.owned[dex/8]|=1u<<(dex%8);
        takeSnapshot();assert(status(id)==CTR_MYSTERY_COMPLETED);unchanged();
    }
    reset();gPlayerParty[0].box.checksum^=0x1111;takeSnapshot();(void)status(CTR_MYSTERY_JIRACHI);unchanged();
    puts("PASS real player-OT level5 gifts, unchanged vanilla creation/RNG, encrypted checksums, party-only capacity and persistent Dex deduplication");
}
static unsigned countDecor(u8 decor) {unsigned n=0;for(unsigned i=0;i<sizeof(world.decorationDolls);i++)n+=world.decorationDolls[i]==decor;return n;}
static void dolls(void) {
    reset();assert(CtrMystery_Activate(CTR_MYSTERY_REGI_DOLLS)==CTR_MYSTERY_RESULT_ACTIVATED);
    assert(countDecor(DECOR_REGIROCK_DOLL)==1 && countDecor(DECOR_REGICE_DOLL)==1 && countDecor(DECOR_REGISTEEL_DOLL)==1);
    takeSnapshot();assert(status(CTR_MYSTERY_REGI_DOLLS)==CTR_MYSTERY_COMPLETED);assert(CtrMystery_Activate(CTR_MYSTERY_REGI_DOLLS)==CTR_MYSTERY_RESULT_ALREADY);unchanged();
    assert(DecorationRemove(DECOR_REGICE_DOLL));assert(CtrMystery_Activate(CTR_MYSTERY_REGI_DOLLS)==CTR_MYSTERY_RESULT_ACTIVATED);assert(countDecor(DECOR_REGICE_DOLL)==1 && countDecor(DECOR_REGIROCK_DOLL)==1);
    reset();assert(DecorationAdd(DECOR_REGIROCK_DOLL));for(unsigned i=1;i<sizeof(world.decorationDolls)-1;i++)world.decorationDolls[i]=DECOR_PIKACHU_DOLL;
    takeSnapshot();assert(status(CTR_MYSTERY_REGI_DOLLS)==CTR_MYSTERY_DECOR_FULL);assert(CtrMystery_Activate(CTR_MYSTERY_REGI_DOLLS)==CTR_MYSTERY_RESULT_NO_SPACE);unchanged();
    reset();failDecorCall=2;takeSnapshot();assert(CtrMystery_Activate(CTR_MYSTERY_REGI_DOLLS)==CTR_MYSTERY_RESULT_FAILED);unchanged();
    puts("PASS missing-doll restoration, owned/placed deduplication, category capacity and rollback on partial insertion failure");
}
int main(void) {sessions();islands();gifts();dolls();puts("All Mystery Events engine checks passed");return 0;}
