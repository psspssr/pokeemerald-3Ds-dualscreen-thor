/* Actual generated Summary input functions; graphics/audio callbacks record
 * effects. No fixture directly replaces a Pokemon move or writes a save. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8, bool8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef void (*TaskFunc)(u8);
#define TRUE 1
#define FALSE 0
#define _(text) text
#define PIXEL_FILL(value) (value)
#define MAX_MON_MOVES 4
#define MOVE_NONE 0
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 160
#define A_BUTTON 1
#define B_BUTTON 2
#define DPAD_LEFT 0x20
#define DPAD_RIGHT 0x10
#define DPAD_UP 0x40
#define DPAD_DOWN 0x80
#define MENU_L_PRESSED 1
#define MENU_R_PRESSED 2
#define SE_SELECT 1
#define SE_FAILURE 2
#define SPRITE_ARR_ID_STATUS 2
#define SPRITE_ARR_ID_MOVE_SELECTOR1 8
#define SPRITE_ARR_ID_MOVE_SELECTOR2 18
#define ARRAY_COUNT(a) (sizeof(a)/sizeof(*(a)))
enum { SUMMARY_MODE_NORMAL, SUMMARY_MODE_LOCK_MOVES, SUMMARY_MODE_BOX, SUMMARY_MODE_SELECT_MOVE };
enum { PSS_PAGE_INFO, PSS_PAGE_SKILLS, PSS_PAGE_BATTLE_MOVES, PSS_PAGE_CONTEST_MOVES };
enum { PSS_LABEL_WINDOW_POKEMON_INFO_TITLE, PSS_LABEL_WINDOW_POKEMON_SKILLS_TITLE,
       PSS_LABEL_WINDOW_BATTLE_MOVES_TITLE, PSS_LABEL_WINDOW_CONTEST_MOVES_TITLE,
       PSS_LABEL_WINDOW_PROMPT_CANCEL, PSS_LABEL_WINDOW_PROMPT_INFO,
       PSS_LABEL_WINDOW_PROMPT_SWITCH, PSS_LABEL_WINDOW_UNUSED1,
       PSS_LABEL_WINDOW_POKEMON_INFO_RENTAL, PSS_LABEL_WINDOW_POKEMON_INFO_TYPE,
       PSS_LABEL_WINDOW_POKEMON_SKILLS_STATS_LEFT, PSS_LABEL_WINDOW_POKEMON_SKILLS_STATS_RIGHT,
       PSS_LABEL_WINDOW_POKEMON_SKILLS_EXP, PSS_LABEL_WINDOW_POKEMON_SKILLS_STATUS,
       PSS_LABEL_WINDOW_MOVES_POWER_ACC, PSS_LABEL_WINDOW_MOVES_APPEAL_JAM,
       PSS_LABEL_WINDOW_UNUSED2, PSS_LABEL_WINDOW_PORTRAIT_DEX_NUMBER,
       PSS_LABEL_WINDOW_PORTRAIT_NICKNAME, PSS_LABEL_WINDOW_PORTRAIT_SPECIES };
static struct { u16 newKeys; u32 vblankCounter1; void (*callback2)(void); } gMain;
static struct { bool8 active; } gPaletteFade;
static struct { s16 data[16]; TaskFunc func; } gTasks[16];
static struct { bool8 invisible; } gSprites[32];
static struct Summary {
    u8 mode, minPageIndex, maxPageIndex, currPageIndex, firstMoveIndex;
    u16 newMove;
    struct { u16 moves[MAX_MON_MOVES]; } summary;
    u8 spriteIds[32], windowIds[8];
} mon, *sMonSummaryScreen;
static struct { bool8 pending; s16 x,y; u32 frame; } sCtrTap;
static u16 gSpecialVar_0x8005;
static u8 sMoveSlotToReplace;
static const u8 gText_Ok[]="OK";
static bool8 linkWait, sliding;
static unsigned closed, warningShown, lastDescription, selectedVisible, printed, filled;
static int changedMon;
static unsigned put[256];
static void MainCB2(void) {}
static void OtherCallback(void) {}
static void OtherTask(u8 id) { (void)id; }
static void Task_ShowPowerAccWindow(u8 id) { (void)id; }
static void Task_HandleReplaceMoveInput(u8);
static void Task_HandleInputCantForgetHMsMoves(u8);
static void ChangeSelectedMove(s16 *,s8,u8 *);
static bool8 CanReplaceMove(void);
static void ShowCantForgetHMsWindow(u8);
static bool8 FuncIsActiveTask(TaskFunc fn)
{
    if(fn==Task_ShowPowerAccWindow) return sliding;
    for(unsigned i=0;i<ARRAY_COUNT(gTasks);i++) if(gTasks[i].func==fn) return TRUE;
    return FALSE;
}
static bool8 MenuHelpers_ShouldWaitForLinkRecv(void) { return linkWait; }
static int GetLRKeysPressed(void) { return 0; }
static bool8 IsMoveHm(u16 move) { return move==99; }
static void StopPokemonAnimations(void) {}
static void PlaySE(int sound) { (void)sound; }
static void BeginCloseSummaryScreen(u8 taskId) { closed++; gTasks[taskId].func=OtherTask; }
static void ChangeSummaryPokemon(u8 id,s8 direction) { (void)id; changedMon+=direction; }
static void ChangePage(u8 id,s8 direction)
{
    (void)id; int page=sMonSummaryScreen->currPageIndex+direction;
    if(page>=sMonSummaryScreen->minPageIndex && page<=sMonSummaryScreen->maxPageIndex)
        sMonSummaryScreen->currPageIndex=page;
}
static void ClearWindowTilemap(u8 id) { (void)id; }
static void PutWindowTilemap(u8 id) { put[id]++; }
static void ScheduleBgCopyTilemapToVram(u8 id) { (void)id; }
static void DrawContestMoveHearts(u16 move) { (void)move; }
static void PrintMoveDetails(u16 move) { lastDescription=move; warningShown=0; }
static void HandlePowerAccTilemap(u16 a,s16 b) { (void)a; (void)b; }
static void HandleAppealJamTilemap(u16 a,s16 b,u16 move) { (void)a; (void)b; (void)move; }
static void KeepMoveSelectorVisible(u8 id) { assert(id==SPRITE_ARR_ID_MOVE_SELECTOR1); selectedVisible++; }
static void PrintHMMovesCantBeForgotten(void) { warningShown++; }
static void SetNewMoveTypeIcon(void) {}
static void CreateMoveSelectorSprites(u8 id) { assert(id==SPRITE_ARR_ID_MOVE_SELECTOR1); }
static bool8 InBattleFactory(void) { return FALSE; }
static bool8 InSlateportBattleTent(void) { return FALSE; }
static void FillWindowPixelBuffer(u8 id,int value)
{ assert(id==PSS_LABEL_WINDOW_PROMPT_CANCEL && value==0); filled++; }
static void PrintTextOnWindow(u8 id,const u8 *text,u8 x,u8 y,u8 spacing,u8 color)
{
    assert(id==PSS_LABEL_WINDOW_PROMPT_CANCEL && y==1 && spacing==0 && color==0);
    if(printed==0) assert(x==4 && !strcmp((const char *)text,"OK"));
    else assert(x==34 && !strcmp((const char *)text,"BACK"));
    printed++;
}

#include "touch.inc"

static void reset(void)
{
    memset(&mon,0,sizeof(mon)); memset(gTasks,0,sizeof(gTasks)); memset(&sCtrTap,0,sizeof(sCtrTap));
    memset(put,0,sizeof(put)); memset(mon.windowIds,255,sizeof(mon.windowIds));
    sMonSummaryScreen=&mon; mon.mode=SUMMARY_MODE_SELECT_MOVE;
    mon.minPageIndex=mon.currPageIndex=PSS_PAGE_BATTLE_MOVES; mon.maxPageIndex=PSS_PAGE_CONTEST_MOVES;
    for(unsigned i=0;i<MAX_MON_MOVES;i++) mon.summary.moves[i]=10+i;
    mon.newMove=50; gTasks[0].func=Task_HandleReplaceMoveInput;
    gMain.newKeys=0; gMain.callback2=MainCB2; gMain.vblankCounter1=100;
    gPaletteFade.active=linkWait=sliding=FALSE;
    gSpecialVar_0x8005=777; sMoveSlotToReplace=77;
    closed=warningShown=selectedVisible=printed=filled=0; changedMon=0; lastDescription=10;
}
static void tap(s16 x,s16 y)
{ CtrSummary_Tap(x,y); gTasks[0].func(0); assert(!sCtrTap.pending); }

int main(void)
{
    for(unsigned row=0;row<=MAX_MON_MOVES;row++) {
        reset(); tap(160,40+16*row);
        assert(mon.firstMoveIndex==row && selectedVisible>0 && closed==0 && gSpecialVar_0x8005==777);
        assert(lastDescription==(row==4?50:10+row));
        tap(192,8); assert(closed==1 && gSpecialVar_0x8005==row);
    }
    reset(); tap(224,8); assert(closed==1 && gSpecialVar_0x8005==MAX_MON_MOVES);
    puts("PASS all five rows preview without accepting; explicit OK/BACK use the existing result paths");

    reset(); mon.summary.moves[1]=mon.summary.moves[3]=MOVE_NONE; mon.newMove=MOVE_NONE;
    tap(160,56); assert(mon.firstMoveIndex==0 && !closed);
    tap(160,88); assert(mon.firstMoveIndex==0 && !closed);
    tap(160,72); assert(mon.firstMoveIndex==2 && !closed);
    tap(160,104); assert(mon.firstMoveIndex==4 && !closed);
    tap(192,8); assert(closed==1 && gSpecialVar_0x8005==4);
    reset(); gMain.newKeys=DPAD_DOWN; gTasks[0].func(0); assert(mon.firstMoveIndex==1);
    gMain.newKeys=A_BUTTON; gTasks[0].func(0); assert(closed==1 && gSpecialVar_0x8005==1);
    reset(); gMain.newKeys=B_BUTTON; gTasks[0].func(0); assert(closed==1 && gSpecialVar_0x8005==4);
    puts("PASS blank rows, move-deleter Cancel row and unchanged controller A/B/navigation");

    reset(); mon.summary.moves[1]=99; tap(160,56); tap(192,8);
    assert(!closed && warningShown && gTasks[0].func==Task_HandleInputCantForgetHMsMoves);
    tap(224,8); assert(!closed && !warningShown && gTasks[0].func==Task_HandleReplaceMoveInput);
    tap(224,8); assert(closed==1 && gSpecialVar_0x8005==4);
    reset(); mon.summary.moves[1]=99; tap(160,56); tap(192,8); tap(160,72);
    assert(!closed && !warningShown && mon.firstMoveIndex==2 && gTasks[0].func==Task_HandleReplaceMoveInput);
    tap(192,8); assert(closed==1 && gSpecialVar_0x8005==2);
    reset(); mon.summary.moves[0]=99; mon.newMove=MOVE_NONE; tap(192,8);
    assert(closed==1 && !warningShown); // Move deleter preserves its native HM rule.
    puts("PASS HM refusal, first-BACK warning dismissal, second-BACK cancellation and safe row recovery");

    for(unsigned mode=0;mode<4;mode++) {
        reset(); CtrSummary_Tap(160,72);
        if(mode==0) gPaletteFade.active=TRUE;
        if(mode==1) linkWait=TRUE;
        if(mode==2) sliding=TRUE;
        if(mode==3) gMain.vblankCounter1+=31;
        gTasks[0].func(0); assert(!sCtrTap.pending && mon.firstMoveIndex==0 && !closed);
        CtrSummary_Tap(160,72);
        if(mode!=3) assert(!sCtrTap.pending);
    }
    reset(); gTasks[0].func=OtherTask; CtrSummary_Tap(160,72); assert(!sCtrTap.pending);
    reset(); gMain.callback2=OtherCallback; sMonSummaryScreen=(struct Summary *)(uintptr_t)1;
    CtrSummary_Tap(160,72); assert(!sCtrTap.pending);
    reset(); CtrSummary_Tap(160,72); Task_SetHandleReplaceMoveInput(0); assert(!sCtrTap.pending);
    reset(); CtrSummary_Tap(160,72); gMain.vblankCounter1=103;
    gTasks[0].func(0); assert(mon.firstMoveIndex==2);
    for(unsigned i=0;i<4;i++) {
        const s16 invalid[][2]={{79,40},{160,31},{160,112},{240,40}};
        reset(); tap(invalid[i][0],invalid[i][1]); assert(mon.firstMoveIndex==0 && !closed);
    }
    puts("PASS stale/fade/link/slide/other-scene taps are discarded and row boundaries stay exact");

    reset(); CtrSummary_PrintMoveTouchButtons(); assert(printed==2 && filled==1);
    PutPageWindowTilemaps(PSS_PAGE_BATTLE_MOVES); assert(put[PSS_LABEL_WINDOW_PROMPT_CANCEL]==1);
    tap(142,8); assert(mon.currPageIndex==PSS_PAGE_CONTEST_MOVES);
    PutPageWindowTilemaps(PSS_PAGE_CONTEST_MOVES); assert(put[PSS_LABEL_WINDOW_PROMPT_CANCEL]==2);
    tap(126,8); assert(mon.currPageIndex==PSS_PAGE_BATTLE_MOVES);
    reset(); mon.mode=SUMMARY_MODE_NORMAL; mon.minPageIndex=0; mon.maxPageIndex=3;
    CtrSummary_PrintMoveTouchButtons(); assert(printed==0 && filled==0);
    PutPageWindowTilemaps(PSS_PAGE_BATTLE_MOVES);
    assert(!put[PSS_LABEL_WINDOW_PROMPT_CANCEL] && put[PSS_LABEL_WINDOW_PROMPT_INFO]==1);
    CtrSummary_Tap(40,40); assert(CtrSummary_TouchInput(0)); assert(changedMon==-1);
    CtrSummary_Tap(200,80); assert(CtrSummary_TouchInput(0)); assert(mon.currPageIndex==PSS_PAGE_CONTEST_MOVES);
    puts("PASS selector header survives page changes; normal four-move Summary keeps its header/navigation");
    return 0;
}
