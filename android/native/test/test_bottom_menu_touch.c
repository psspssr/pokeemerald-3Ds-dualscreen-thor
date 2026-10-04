#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "android_bottom_menus.h"
#include "3ds_input.h"

typedef uint8_t u8;
typedef uint8_t bool8;
typedef int16_t s16;
#define TRUE 1
#define FALSE 0
#define W 320
#define H 240
#define CW 240
enum { HIT_NONE, HIT_MAP, HIT_NAV, SCR_OPTION, BAG_VIEW_WHOLE };
enum { BAG_TOUCH_DOWN, BAG_TOUCH_MOVE, BAG_TOUCH_UP, BAG_TOUCH_CANCEL };
#include "bottom_state.inc"

static struct { u8 mode,bagView; } sShown;
static int sScreen,sOptionScroll,sOptionScrollStart;
static CtrInput input;
static CtrHostBottomMenuContent presented;
static bool storageOpen, navOpen, dexOpen, partyTaskActive, bottomWhole;
static bool sInGame;
static void *gSaveBlock1Ptr, *gSaveBlock2Ptr, *gBagMenu;
static void CB2_Overworld(void) {}
static void CB2_BagMenuRun(void) {}
static void BattleMainCB2(void) {}
static void PartyCallback(void) {}
static void SummaryCallback(void) {}
static void StorageCallback(void) {}
static void Task_HandleChooseMonInput(u8 taskId) { (void)taskId; }
static void (*sPartyMenuCallback)(void);
static struct { void (*callback2)(void); bool inBattle; } gMain;
static struct { bool active; } gPaletteFade;
static struct { int kind; } sAsked;
enum { ASK_NONE, ASK_ACTION, ASK_MOVE, ASK_TARGET };
static bool CtrPokenav_IsOpen(void) { return navOpen; }
static bool CtrPokedex_IsOpen(void) { return dexOpen; }
bool CtrVideo_BottomWhole(void) { return bottomWhole; }
static bool FuncIsActiveTask(void (*fn)(u8))
{ return partyTaskActive && fn==Task_HandleChooseMonInput; }
static struct { int kind,phase,x,y; } calls[32];
static unsigned count,reads;
static void record(int kind,int phase,int x,int y)
{ assert(count<32); calls[count++]=(typeof(*calls)){kind,phase,x,y}; }
const CtrInput *CtrInput_Get(void) { return &input; }
CtrHostBottomMenuContent CtrHost_PresentedBottomMenuContent(void) { reads++; return presented; }
static bool8 BagShown(u8 mode) { return mode==MODE_BAG_MENU; }
static bool8 DexShown(u8 mode) { return mode==MODE_POKEDEX; }
static void CtrBag_Touch(u8 phase,s16 x,s16 y) { record(1,phase,x,y); }
static void CtrPokedex_Touch(u8 phase,s16 x,s16 y) { record(2,phase,x,y); }
static void CtrMenu_PostTap(s16 x,s16 y) { record(3,0,x,y); }
static bool8 CtrStorage_IsOpen(void) { return storageOpen; }
static void CtrStorage_Tap(s16 x,s16 y) { record(4,0,x,y); }
static void CtrSummary_Tap(s16 x,s16 y) { record(5,0,x,y); }
int CtrVideo_BottomPictureY(int y) { return y+9; }
static void NavTap(int x,int y) { record(6,0,x,y); }
static void NavSwipe(int dy) { record(7,0,0,dy); }
static void OptionsDrag(int dy) { record(8,0,0,dy); }
static u8 HitTest(int x,int y) { (void)y; return x>=CW?HIT_NAV:HIT_MAP; }
static void Activate(u8 hit,u8 mode) { record(9,mode,hit,0); }
#include "bottom_mode.inc"
#include "bottom_touch.inc"

static void reset(u8 mode,bool whole,CtrHostBottomMenuContent content)
{
    memset(&sTouch,0,sizeof(sTouch)); input=(CtrInput){0};
    sShown.mode=mode; sShown.bagView=whole?BAG_VIEW_WHOLE:0;
    presented=content; ProcessTouch(mode); count=reads=0;
    sScreen=0; storageOpen=false;
}
static void event(unsigned phase,unsigned x,unsigned y)
{
    input=(CtrInput){.touchDown=phase==BAG_TOUCH_DOWN,.touchUp=phase==BAG_TOUCH_UP,
                     .touchActive=phase!=BAG_TOUCH_UP,.touchX=x,.touchY=y};
    unsigned before=reads; ProcessTouch(sShown.mode); assert(reads==before+1);
}
static void expect(unsigned n,int kind,int phase,int x,int y)
{ assert(n<count && calls[n].kind==kind && calls[n].phase==phase && calls[n].x==x && calls[n].y==y); }

static void classifiedEvent(unsigned phase,unsigned x,unsigned y)
{
    input=(CtrInput){.touchDown=phase==BAG_TOUCH_DOWN,.touchUp=phase==BAG_TOUCH_UP,
                     .touchActive=phase!=BAG_TOUCH_UP,.touchX=x,.touchY=y};
    ProcessTouch(CurrentMode());
}

static void testClassifiedMenus(void)
{
    enum { BATTLE_PARTY, FIELD_PARTY, SUMMARY, PC_STORAGE, PC_SUMMARY, WHOLE_BAG, POKENAV, POKEDEX };
    const int expectedModes[]={MODE_PARTY_MENU,MODE_PARTY_MENU,MODE_STORAGE,MODE_STORAGE,
                               MODE_STORAGE,MODE_BAG_MENU,MODE_POKENAV,MODE_POKEDEX};
    const int expectedKinds[]={3,3,5,4,5,1,6,2};
    for(unsigned scene=0;scene<sizeof(expectedModes)/sizeof(*expectedModes);scene++) {
        bool whole=scene!=FIELD_PARTY && scene!=POKENAV && scene!=POKEDEX;
        CtrHostBottomMenuContent content=scene==POKENAV?CTR_HOST_BOTTOM_ORIGINAL:
            whole?CTR_HOST_BOTTOM_WHOLE:CTR_HOST_BOTTOM_FIELD;
        reset(MODE_OFF,whole,content);
        sInGame=true; gSaveBlock1Ptr=gSaveBlock2Ptr=(void *)1; gBagMenu=NULL;
        navOpen=dexOpen=partyTaskActive=bottomWhole=false;
        gPaletteFade.active=false; sAsked.kind=ASK_NONE;
        gMain.inBattle=scene==BATTLE_PARTY || scene==WHOLE_BAG;
        gMain.callback2=SummaryCallback;
        sPartyMenuCallback=scene>=SUMMARY?PartyCallback:NULL; // remembered Party must not steal another screen
        if(scene==BATTLE_PARTY || scene==FIELD_PARTY) {
            gMain.callback2=PartyCallback; partyTaskActive=true;
        }
        if(scene==PC_STORAGE) { gMain.callback2=StorageCallback; storageOpen=true; }
        if(scene==WHOLE_BAG) { gMain.callback2=CB2_BagMenuRun; gBagMenu=(void *)1; }
        if(scene==POKENAV) navOpen=true;
        if(scene==POKEDEX) dexOpen=true;
        bottomWhole=whole;
        // Use the classifier for the shown snapshot AND both touch events;
        // selecting a requested mode by hand would hide the battle Party bug.
        sShown.mode=CurrentMode();
        classifiedEvent(BAG_TOUCH_DOWN,120,90);
        if(scene==BATTLE_PARTY || scene==FIELD_PARTY) partyTaskActive=false;
        classifiedEvent(BAG_TOUCH_UP,120,90);
        fprintf(stderr,"menu scene%u: classified%d expected%d, touch handler%d expected%d\n",
                scene,sShown.mode,expectedModes[scene],count?calls[0].kind:0,expectedKinds[scene]);
        assert(sShown.mode==expectedModes[scene] && count>0 && calls[0].kind==expectedKinds[scene]);
        if(scene==BATTLE_PARTY || scene==FIELD_PARTY)
            assert(CurrentMode()==MODE_PARTY_MENU); // dialog task changed, same live Party callback
        if(scene==SUMMARY || scene==PC_SUMMARY) {
            assert(count==1); expect(0,5,0,90,60);
        } else if(scene==PC_STORAGE) {
            assert(count==1); expect(0,4,0,90,60);
        } else if(scene==BATTLE_PARTY) {
            assert(count==1); expect(0,3,0,90,60);
        } else if(scene==FIELD_PARTY) {
            assert(count==1); expect(0,3,0,120,60);
        }
        // Leaving Party must not preserve its input mode just because its
        // previously observed callback is still remembered.
        gMain.callback2=CB2_Overworld; gMain.inBattle=false;
        storageOpen=navOpen=dexOpen=bottomWhole=false; gBagMenu=NULL;
        assert(CurrentMode()==MODE_FIELD);
        sInGame=false; gMain.callback2=SummaryCallback;
        assert(CurrentMode()==MODE_OFF);
    }
    puts("PASS actual menu classification and touch routing: battle/field Party and dialogs, Summary, PC/PCSummary, BagWhole, PokeNav and Pokedex");
}

int main(void)
{
    testClassifiedMenus();
    for(unsigned scene=0;scene<=CTR_CENTRED_SCREENS+2;scene++) {
        CtrHostBottomMenuContent expected=CTR_HOST_BOTTOM_ORIGINAL;
        if(scene==CTR_CENTRED_STORAGE || scene==CTR_CENTRED_SUMMARY || scene==CTR_CENTRED_BAG_WHOLE || scene==CTR_CENTRED_PARTY_WHOLE)
            expected=CTR_HOST_BOTTOM_WHOLE;
        if(scene==CTR_CENTRED_BAG || scene==CTR_CENTRED_POKEDEX || scene==CTR_CENTRED_PARTY)
            expected=CTR_HOST_BOTTOM_FIELD;
        assert(AndroidBottomMenu_Content(scene)==expected);
    }
    /* Every native touch coordinate keeps the original mapping when off.
     * Expanded modes map the entire active source including all four edges. */
    for(int mode=0;mode<3;mode++) for(int x=0;x<320;x++) for(int y=0;y<240;y++) {
        int sx=x,sy=y; CtrBottomContent_SourcePoint(mode,&sx,&sy);
        assert(sx==(mode==CTR_HOST_BOTTOM_WHOLE?40+x*3/4:x));
        assert(sy==(mode==CTR_HOST_BOTTOM_ORIGINAL?y:40+y*2/3));
    }
    int x=-1,y=-1; CtrBottomContent_SourcePoint(CTR_HOST_BOTTOM_WHOLE,&x,&y);
    assert(x==39 && y==39); // original GBA origin subtraction leaves both negative
    for(int width=1;width<=2000;width++) {
        CtrBottomContentRegion r[2]; CtrHostRect panel={13,29,width,1080};
        unsigned n=CtrBottomContent_Regions(CTR_HOST_BOTTOM_FIELD,panel,r);
        int edge=(width*3+3)/4;
        assert(r[0].destination.w==edge && r[0].source.x==0 && r[0].source.y==40);
        if(n==2) {
            assert(r[1].destination.x==13+edge && r[1].destination.w+edge==width);
            assert((edge-1)*320/width<240 && edge*320/width>=240);
            assert(r[1].source.x==240 && r[1].source.y==0 && r[1].source.w==80 && r[1].source.h==240);
        } else assert(edge==width);
    }
    CtrBottomContentRegion r[2];
    assert(CtrBottomContent_Regions(CTR_HOST_BOTTOM_ORIGINAL,(CtrHostRect){0,0,0,1080},r)==0);
    assert(CtrBottomContent_Regions(CTR_HOST_BOTTOM_WHOLE,(CtrHostRect){0,0,1240,1080},r)==1);
    assert(r[0].source.x==40 && r[0].source.y==40 && r[0].source.w==240 && r[0].source.h==160);
    assert(r[0].destination.w==1240 && r[0].destination.h==1080);

    for(int enabled=0;enabled<2;enabled++) for(int whole=0;whole<2;whole++) {
        CtrHostBottomMenuContent content=enabled?(whole?CTR_HOST_BOTTOM_WHOLE:CTR_HOST_BOTTOM_FIELD):CTR_HOST_BOTTOM_ORIGINAL;
        int width=whole?320:240,ox=whole?40:0;
        reset(MODE_BAG_MENU,whole,content);
        event(BAG_TOUCH_DOWN,0,0); event(BAG_TOUCH_MOVE,width-1,239); event(BAG_TOUCH_UP,width-1,239);
        assert(count==3);
        expect(0,1,BAG_TOUCH_DOWN,enabled?0:-ox,enabled?0:-40);
        expect(1,1,BAG_TOUCH_MOVE,enabled?239:width-1-ox,enabled?159:199);
        expect(2,1,BAG_TOUCH_UP,0,0);
        reset(MODE_PARTY_MENU,whole,content);
        event(BAG_TOUCH_DOWN,width-1,239); event(BAG_TOUCH_UP,width-1,239);
        assert(count==1); expect(0,3,0,enabled?239:width-1-ox,enabled?159:199);
    }
    reset(MODE_POKEDEX,false,CTR_HOST_BOTTOM_FIELD);
    event(BAG_TOUCH_DOWN,0,0); event(BAG_TOUCH_MOVE,239,239); event(BAG_TOUCH_UP,239,239);
    expect(0,2,BAG_TOUCH_DOWN,0,0); expect(1,2,BAG_TOUCH_MOVE,239,159); expect(2,2,BAG_TOUCH_UP,0,0);
    for(int storage=0;storage<2;storage++) {
        reset(MODE_STORAGE,true,CTR_HOST_BOTTOM_WHOLE); storageOpen=storage;
        event(BAG_TOUCH_DOWN,319,239); event(BAG_TOUCH_UP,319,239);
        assert(count==1); expect(0,storage?4:5,0,239,159);
    }
    /* Field navigation never enters the content inverse. An existing bag
     * drag can leave the content, and must remain out of bounds there. */
    reset(MODE_PARTY_MENU,false,CTR_HOST_BOTTOM_FIELD);
    event(BAG_TOUCH_DOWN,300,25); event(BAG_TOUCH_UP,300,25);
    assert(count==1); expect(0,9,MODE_PARTY_MENU,HIT_NAV,0);
    reset(MODE_BAG_MENU,false,CTR_HOST_BOTTOM_FIELD);
    event(BAG_TOUCH_DOWN,40,60); event(BAG_TOUCH_MOVE,319,239);
    expect(1,1,BAG_TOUCH_MOVE,319,159);
    presented=CTR_HOST_BOTTOM_ORIGINAL; event(BAG_TOUCH_MOVE,319,239);
    assert(count==4); expect(2,1,BAG_TOUCH_CANCEL,0,0); expect(3,2,BAG_TOUCH_CANCEL,0,0);
    event(BAG_TOUCH_UP,319,239); assert(count==4);
    event(BAG_TOUCH_DOWN,20,80); expect(4,1,BAG_TOUCH_DOWN,20,40);
    /* A different game mode also preserves upstream's existing cancel path. */
    unsigned before=count; ProcessTouch(MODE_FIELD); assert(count==before+2 && !sTouch.active);
    reset(MODE_POKENAV,false,CTR_HOST_BOTTOM_ORIGINAL);
    event(BAG_TOUCH_DOWN,120,60); event(BAG_TOUCH_UP,120,60);
    expect(0,6,0,120,69);
    reset(MODE_FIELD,false,CTR_HOST_BOTTOM_ORIGINAL); sScreen=SCR_OPTION;
    event(BAG_TOUCH_DOWN,20,50); event(BAG_TOUCH_MOVE,20,90);
    expect(0,8,0,0,40);
    puts("PASS bottom content: complete scene whitelist, all230400 virtual points, source rectangles/sidebar splits, real Bag/Dex drags, Party/Storage/Summary taps, canceled layout transitions and unchanged Nav/options");
    return 0;
}
