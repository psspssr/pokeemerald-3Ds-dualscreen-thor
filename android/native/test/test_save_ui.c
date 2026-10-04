#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "constants/characters.h"
typedef uint8_t u8, bool8;
typedef uint16_t u16;
#define TRUE 1
#define FALSE 0
#define ARRAY_COUNT(a) (sizeof(a)/sizeof(*(a)))
enum {SAVE_ASK,SAVE_OVERWRITE,SAVE_DONE,BOX_MESSAGE,HIT_YES,HIT_NO,HIT_OK,TXT_WHITE,TXT_DARK,CTR_LOG_ERROR};
static const u16 gFontNormalLatinGlyphs[1],gFontSmallLatinGlyphs[1];
#include "save_data.inc"
static Font sSmall,sNormal;
static u8 sSaveStep;
static const u8 gText_Yes[]={0xff},gText_No[]={0xff};
static const u16 *ResolveFontBase(const u16 *p){return p;}
#if TEST_BOUNDED
static const u8 *Port_ResolveTextPointer(const u8 *p){return p;}
static void CtrLog_Write(int kind,const char *text){(void)kind;(void)text;}
#endif
static const u8 nestedName[]={PLACEHOLDER_BEGIN,PLACEHOLDER_ID_PLAYER,EOS};
static const u8 cycle[]={PLACEHOLDER_BEGIN,PLACEHOLDER_ID_STRING_VAR_2,EOS};
static const u8 empty[]={EOS};
static const u8 *GetExpandedPlaceholder(unsigned id)
{
    if(id==PLACEHOLDER_ID_PLAYER)return playerName;
    if(id==PLACEHOLDER_ID_STRING_VAR_1)return nestedName;
    if(id==PLACEHOLDER_ID_STRING_VAR_2)return cycle;
    return empty;
}
static u8 GetExtCtrlCodeLength(u8 code);
static const u8 *Ascii(const char *s){(void)s;static const u8 error[]={CHAR_E,CHAR_R,CHAR_R,EOS};return error;}
static struct {int x,y,w,h;} box,buttons[2];
static unsigned buttonCount,glyphCount;
static int maxX,maxY;
static void DrawBox(int kind,int x,int y,int wt,int ht)
{assert(kind==BOX_MESSAGE);box=(typeof(box)){x,y,wt*8,ht*8};}
static int DrawGlyph(const Font *font,u16 glyph,int x,int y,u16 fg,u16 shadow)
{
    (void)fg;(void)shadow;
    assert(x>=box.x+4 && y>=box.y+4);
    int right=x+font->widths[glyph],bottom=y+font->height;
    if(right>maxX)maxX=right;
    if(bottom>maxY)maxY=bottom;
    assert(right<=box.x+box.w-4 && bottom<=box.y+box.h-4);
    glyphCount++;return font->widths[glyph];
}
static void DrawLabelButton(int x,int y,int wt,int ht,const u8 *text,bool8 pressed,bool8 enabled,int hit)
{
    (void)text;(void)pressed;(void)enabled;
    assert(hit==HIT_YES || hit==HIT_NO || hit==HIT_OK);
    assert(buttonCount<2 && x>=0 && y>=box.y+box.h && x+wt*8<=240 && y+ht*8<=240);
    buttons[buttonCount++]=(typeof(box)){x,y,wt*8,ht*8};
}
enum {SCR_MAP,SCR_SAVE,SAVE_STATUS_EMPTY,SAVE_STATUS_CORRUPT,SAVE_STATUS_OK,SAVE_NORMAL,SAVE_OVERWRITE_DIFFERENT_FILE,GAME_STAT_SAVED_GAME,SE_SAVE};
static u8 sScreen,gSaveFileStatus,gDifferentSaveFile;
static bool fieldIdle=true;
static unsigned writes,lastSaveKind;
static const u8 *gText_ConfirmSave=textConfirmSave,*gText_AlreadySavedFile=textAlreadySavedFile;
static const u8 *gText_DifferentSaveFile=textDifferentSaveFile,*gText_PlayerSavedGame=textPlayerSavedGame,*gText_SaveError=textSaveError;
static bool FieldIdle(void){return fieldIdle;}
static void SaveMapView(void){}
static void IncrementGameStat(int stat){assert(stat==GAME_STAT_SAVED_GAME);}
static u8 TrySavingData(int type){writes++;lastSaveKind=type;return SAVE_STATUS_OK;}
static void PlaySE(int sound){assert(sound==SE_SAVE);}
#include "save_helpers.inc"

int main(void)
{
    (void)ActivateSave; // keep the --before overflow probe independent of action tests
    ResolveFonts();
    const u8 *cases[]={textDifferentSaveFile,textConfirmSave,textAlreadySavedFile,textPlayerSavedGame,textSaveError};
    const unsigned steps[]={SAVE_OVERWRITE,SAVE_ASK,SAVE_OVERWRITE,SAVE_DONE,SAVE_DONE};
    for(unsigned i=0;i<ARRAY_COUNT(cases);i++) {
        u8 reference[1024];StringExpandPlaceholders(reference,cases[i]);
        size_t length=0;while(reference[length]!=EOS)length++;
        fprintf(stderr,"save case%u: expanded%zu bytes +EOS, Save capacity%zu, snapshot capacity%zu\n",
                i,length,sizeof(sSaveMessage),sizeof(((ViewState *)0)->text));
        sSaveStep=steps[i];SetSaveMessage(cases[i]);
        assert(sSaveStep==steps[i] && length<sizeof(sSaveMessage));
        assert(!memcmp(reference,sSaveMessage,length+1));
        ViewState state={.saveStep=sSaveStep,.canSave=true};
        CopyText(state.text,sizeof(state.text),sSaveMessage);
        assert(length<sizeof(state.text) && !memcmp(reference,state.text,length+1));
        buttonCount=glyphCount=0;maxX=maxY=0;DrawSave(&state);
        assert(glyphCount>0 && buttonCount==(steps[i]==SAVE_DONE?1u:2u));
        if(buttonCount==2)assert(buttons[0].x+buttons[0].w<=buttons[1].x);
        fprintf(stderr,"save case%u: glyph bounds right%d bottom%d, box(%d,%d %dx%d), buttonsY%d\n",
                i,maxX,maxY,box.x,box.y,box.w,box.h,buttons[0].y);
    }
#if TEST_BOUNDED
    u8 tooLong[1024];memset(tooLong,CHAR_W,sizeof(tooLong));tooLong[sizeof(tooLong)-1]=EOS;
    sSaveStep=SAVE_OVERWRITE;SetSaveMessage(tooLong);
    assert(sSaveStep==SAVE_DONE); // an incomplete overwrite warning cannot enable Yes
    const u8 incompletePlaceholder[]={PLACEHOLDER_BEGIN,EOS};
    const u8 incompleteControl[]={EXT_CTRL_CODE_BEGIN,EXT_CTRL_CODE_COLOR,EOS};
    const u8 unknownControl[]={EXT_CTRL_CODE_BEGIN,0x80,EOS};
    const u8 *invalid[]={tooLong,incompletePlaceholder,incompleteControl,unknownControl,cycle,NULL};
    for(unsigned i=0;i<ARRAY_COUNT(invalid);i++) {
        sScreen=SCR_SAVE;sSaveStep=SAVE_OVERWRITE;writes=0;
        SetSaveMessage(invalid[i]);assert(sSaveStep==SAVE_DONE);
        ActivateSave(HIT_YES); // stale touch/controller-confirm must not bypass the error
        assert(!writes && sSaveStep==SAVE_DONE);
        ActivateSave(HIT_OK);assert(!writes && sScreen==SCR_MAP);
    }
    const u8 nested[]={PLACEHOLDER_BEGIN,PLACEHOLDER_ID_STRING_VAR_1,EOS};
    sSaveStep=SAVE_ASK;SetSaveMessage(nested);assert(!memcmp(sSaveMessage,playerName,sizeof(playerName)));
    const u8 escapedPlaceholderByte[]={EXT_CTRL_CODE_BEGIN,EXT_CTRL_CODE_COLOR,PLACEHOLDER_BEGIN,CHAR_A,EOS};
    SetSaveMessage(escapedPlaceholderByte);
    assert(!memcmp(sSaveMessage,escapedPlaceholderByte,sizeof(escapedPlaceholderByte)));
    // Test the real initial-question -> warning -> write path, not just the
    // formatter. A failed warning removes every acceptance route before I/O.
    sScreen=SCR_SAVE;gSaveFileStatus=SAVE_STATUS_OK;gDifferentSaveFile=true;writes=0;OpenSave();
    gText_DifferentSaveFile=tooLong;ActivateSave(HIT_YES);
    assert(sSaveStep==SAVE_DONE && !writes);
    ActivateSave(HIT_YES);assert(!writes);
    ActivateSave(HIT_NO);assert(sScreen==SCR_MAP && !writes);
    gText_DifferentSaveFile=textDifferentSaveFile;
    sScreen=SCR_SAVE;OpenSave();ActivateSave(HIT_YES);
    assert(sSaveStep==SAVE_OVERWRITE && !writes);
    ActivateSave(HIT_YES);assert(writes==1 && lastSaveKind==SAVE_OVERWRITE_DIFFERENT_FILE && sSaveStep==SAVE_DONE);
    ActivateSave(HIT_OK);assert(writes==1 && sScreen==SCR_MAP);
    gDifferentSaveFile=false;sScreen=SCR_SAVE;OpenSave();ActivateSave(HIT_YES);ActivateSave(HIT_NO);
    assert(writes==1 && sScreen==SCR_MAP);
    sScreen=SCR_SAVE;gSaveFileStatus=SAVE_STATUS_EMPTY;OpenSave();ActivateSave(HIT_YES);
    assert(writes==2 && lastSaveKind==SAVE_NORMAL);
    u8 small[4]={1,2,3,4},*out=small;unsigned left=3;
    assert(!AppendSaveMessage(&out,&left,tooLong,0));assert(out==small+3 && left==0 && small[3]==4);
#endif
    puts("PASS Save UI: all5 real messages and max-length player name, bounded expansion/full snapshots, actual glyph/control-code line bounds and unchanged Yes/No/OK semantics");
    return 0;
}
