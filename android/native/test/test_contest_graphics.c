#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gba/defines.h"
typedef uint8_t u8, bool8;
typedef int8_t s8;
typedef int16_t s16;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
enum { APPLAUSE_METER_SIZE=5, MAX_MON_MOVES=4, MOVE_NONE=0, MOVE_WINDOWS_START=5,
       FONT_NORMAL, FONT_NARROW, TEXT_SKIP_DRAW, WINDOW_TILE_DATA, CONTEST_EFFECT_REPETITION_NOT_BORING };
#define ARRAY_COUNT(a) (sizeof(a)/sizeof(*(a)))
#define PIXEL_FILL(n) (n)
static u8 vram[0x20000] __attribute__((aligned(4)));
#undef OBJ_VRAM0
#undef BG_CHAR_ADDR
#define OBJ_VRAM0 ((uintptr_t)vram)
#define BG_CHAR_ADDR(n) ((uintptr_t)(vram+0x10000+(n)*0x2000))
static u8 applause[128] __attribute__((aligned(4)));
static u8 icon[1024] __attribute__((aligned(4)));
static u8 border[256] __attribute__((aligned(4)));
#ifdef PORT_BRIDGE
static const u8 gContestApplauseMeterGfx[1] __attribute__((aligned(4))) = {0};
static const u8 iconStub[1] __attribute__((aligned(4))) = {0};
static const u8 sResultsTextWindow_Gfx[1] __attribute__((aligned(4))) = {0};
static const void *Port_ResolveAssetPointer(const void *p) __attribute__((unused));
static const void *Port_ResolveAssetPointer(const void *p)
{
    if(p==gContestApplauseMeterGfx)return applause;
    if(p==iconStub)return icon;
    if(p==sResultsTextWindow_Gfx)return border;
    return p;
}
static bool Port_LoadAssetPointerToBufferSized(const void *p,void *dst,u32 bytes)
{
    const void *resolved=Port_ResolveAssetPointer(p);
    if(resolved==p)return false;
    memcpy(dst,resolved,bytes);return true;
}
static bool Port_LoadAssetPointerToBuffer(const void *p,void *dst,u32 bytes)
{return Port_LoadAssetPointerToBufferSized(p,dst,bytes);}
#else
#define gContestApplauseMeterGfx applause
#define sResultsTextWindow_Gfx border
#define iconStub icon
#endif
static struct Sprite {struct {unsigned tileNum;}oam;s16 data[8];}gSprites[4];
static struct {s8 applauseLevel;u8 applauseMeterSpriteId,playerMoveChoice;}eContest;
static struct {u16 moves[4];}gContestMons[4];
static struct {u16 prevMove;bool hasJudgesAttention;}eContestantStatus[4];
static struct {u8 effect;}gContestMoves[4];
static const u8 gText_ColorLightShadowDarkGray[]={0},gText_ColorBlue[]={0};
static const u8 gMoveNames[4][16]={{'-',0},{'S',0},{'G',0},{'F',0}};
static u8 gContestPlayerMonIndex;
static struct {void(*func)(u8);}gTasks[1];
static u16 gBattle_BG0_Y,gBattle_BG2_Y;
static unsigned overflowCalls,drawnMoves,describedMove,tilesWritten,expectedMon;
static void CpuCopy32(const void *src,void *dst,u32 size);
static void CpuFill32(u32 value,void *dst,u32 size){assert(!value);memset(dst,0,size);}
static u8 StartApplauseOverflowAnimation(void){overflowCalls++;return 0;}
static bool IsContestantAllowedToCombo(unsigned i){(void)i;return false;}
static bool AreMovesContestCombo(unsigned a,unsigned b){(void)a;(void)b;return false;}
static u8 *StringCopy(u8 *out,const u8 *in){size_t n=strlen((const char *)in);memcpy(out,in,n+1);return out+n;}
static void FillWindowPixelBuffer(unsigned win,unsigned fill){(void)win;(void)fill;}
static void Contest_PrintTextToBg0WindowAt(unsigned win,const u8 *s,unsigned x,unsigned y,unsigned font)
{assert(win>=5&&win<=8&&s&&x==5&&y==1&&font==FONT_NARROW);drawnMoves++;}
static void DrawMoveSelectArrow(unsigned i){assert(i==eContest.playerMoveChoice);}
static void PrintContestMoveDescription(unsigned move){describedMove=move;}
static void Task_HandleMoveSelectInput(u8 id){(void)id;}
static const u8 *GetMonIconPtr(u16 species,u32 personality,u32 handleDeoxys)
{assert(species==25&&personality==123&&handleDeoxys==(expectedMon==gContestPlayerMonIndex));return iconStub;}
static void RequestDma3Copy(const void *src,void *dst,u32 size,unsigned mode)
{assert(mode==1);CpuCopy32(src,dst,size);}
static void WriteSequenceToBgTilemapBuffer(unsigned bg,unsigned tile,unsigned x,unsigned y,unsigned w,unsigned h,unsigned delta,unsigned mode)
{assert(bg==1&&tile==(((expectedMon+10)<<12)|(expectedMon*16+0x200))&&x==3&&y==expectedMon*3+4&&w==4&&h==3&&delta==17&&mode==1);tilesWritten++;}
struct WindowTemplate {unsigned width,height;};
static u8 windowTiles[3200] __attribute__((aligned(4)));
static unsigned textWidth,windowWidth,removed;
static const u8 sContestLinkTextColors[4]={0};
static u16 AddWindow(const struct WindowTemplate *w){windowWidth=w->width;assert(w->height==2);return 0;}
static int GetStringWidth(unsigned font,const u8 *text,unsigned spacing){assert(font==FONT_NORMAL&&text&&!spacing);return textWidth;}
static void AddTextPrinterParameterized3(unsigned window,unsigned font,int x,unsigned y,const u8 *colors,unsigned speed,const u8 *text)
{(void)x;assert(!window&&font==FONT_NORMAL&&y==1&&colors==sContestLinkTextColors&&speed==TEXT_SKIP_DRAW&&text);}
static uintptr_t GetWindowAttribute(unsigned window,unsigned attr){assert(!window&&attr==WINDOW_TILE_DATA);return (uintptr_t)windowTiles;}
static void RemoveWindow(unsigned window){assert(!window);removed++;}
#include "contest_helpers.inc"
static void CpuCopy32(const void *src,void *dst,u32 size){CpuSet(src,dst,0x04000000|(size/4));}

static void applauseTest(void)
{
    for(unsigned level=0;level<=5;level++) {
        memset(vram,0xa5,sizeof(vram));gSprites[0].oam.tileNum=2;eContest.applauseMeterSpriteId=0;eContest.applauseLevel=level;overflowCalls=0;
        u8 expected[sizeof(vram)];memcpy(expected,vram,sizeof(expected));
        for(unsigned i=0;i<5;i++) {
            const u8 *src=applause+(i<level?64:0);
            memcpy(expected+(2+17+i)*32,src,32);memcpy(expected+(2+25+i)*32,src+32,32);
        }
        UpdateApplauseMeter();assert(!memcmp(expected,vram,sizeof(vram)));
        assert(overflowCalls==(level==5?5u:0u));
    }
    puts("PASS real applause update/CpuSet: levels0-5 and both tile halves, no neighboring VRAM changes");
}
static void iconsTest(void)
{
    gContestPlayerMonIndex=2;
    for(expectedMon=0;expectedMon<4;expectedMon++)for(unsigned frame=0;frame<2;frame++)for(unsigned draw=0;draw<2;draw++) {
        memset(vram,0xa5,sizeof(vram));tilesWritten=0;u8 expected[sizeof(vram)];memcpy(expected,vram,sizeof(expected));
        memcpy(expected+0x12000+expectedMon*0x200,icon+frame*0x200+0x80,0x180);
        LoadContestMonIcon(25,expectedMon,frame,draw,123);
        assert(!memcmp(expected,vram,sizeof(vram))&&tilesWritten==draw);
    }
    puts("PASS real results icon DMA: four contestants, two bounce frames, both paths and original Deoxys flag");
}
static void textTest(void)
{
    const unsigned widths[]={64,128,192,224};
    for(unsigned w=0;w<ARRAY_COUNT(widths);w++) {
        textWidth=widths[w];removed=0;memset(vram,0xa5,sizeof(vram));
        for(unsigned i=0;i<4;i++){gSprites[i].oam.tileNum=i*32;gSprites[0].data[i?i-1:0]=i;}
        unsigned tiles=(textWidth+9)/8;u8 expected[sizeof(vram)];memcpy(expected,vram,sizeof(expected));memset(expected,0,4096);
        for(unsigned col=0;col<tiles+2;col++) {
            unsigned pos=(col/8)*1024+(col%8)*32;
            unsigned top=col==0?0:col==tiles+1?32:192;
            unsigned bottom=col==0?64:col==tiles+1?96:224;
            memcpy(expected+pos,border+top,32);memcpy(expected+pos+0x300,border+bottom,32);
            if(col==0||col==tiles+1) {
                unsigned edge=col==0?128:160;memcpy(expected+pos+0x100,border+edge,32);memcpy(expected+pos+0x200,border+edge,32);
            }else{
                memcpy(expected+pos+0x100,windowTiles+(col-1)*32,32);memcpy(expected+pos+0x200,windowTiles+960+(col-1)*32,32);
            }
        }
        s32 x=DrawResultsTextWindow((const u8 *)"result",0);
        assert(removed==1&&windowWidth==30&&x==(240-(int)(tiles+2)*8)/2);
        assert(!memcmp(expected,vram,sizeof(vram)));
    }
    puts("PASS real results text-window borders/text rows: four widths and exact four-sprite buffers");
}
#define NAME_(x) #x
#define NAME(x) NAME_(x)
static void geometryTest(const char *target)
{
    fprintf(stderr,"%s target: %dx%d, %s\n",target,DISPLAY_WIDTH,DISPLAY_HEIGHT,NAME(SetVBlankCallback));
    if(!strcmp(target,"contest")) {
        gContestPlayerMonIndex=0;eContest.playerMoveChoice=2;drawnMoves=0;
        for(unsigned i=0;i<4;i++)gContestMons[0].moves[i]=i;
        Task_ShowMoveSelectScreen(0);
        fprintf(stderr,"actual Contest move scroll BG0=%u BG2=%u\n",gBattle_BG0_Y,gBattle_BG2_Y);
        assert(gBattle_BG0_Y==160&&gBattle_BG2_Y==160&&drawnMoves==4&&describedMove==2&&gTasks[0].func==Task_HandleMoveSelectInput);
        puts("PASS actual Contest move-selector scroll=160, original four moves/selected description/input handler");
    }
    assert(DISPLAY_WIDTH==240&&DISPLAY_HEIGHT==160&&!strcmp(NAME(SetVBlankCallback),"CtrCentred_SetVBlankCallback"));
}
int main(int argc,char **argv)
{
    assert(argc==3);
    for(unsigned i=0;i<sizeof(applause);i++)applause[i]=(i*17+3)&255;
    for(unsigned i=0;i<sizeof(icon);i++)icon[i]=(i*7+i/512+13)&255;
    for(unsigned i=0;i<sizeof(border);i++)border[i]=(i*11+5)&255;
    for(unsigned i=0;i<sizeof(windowTiles);i++)windowTiles[i]=(i*3+1)&255;
    bool all=!strcmp(argv[1],"all");
    if(all||!strcmp(argv[1],"applause"))applauseTest();
    if(all||!strcmp(argv[1],"icons"))iconsTest();
    if(all||!strcmp(argv[1],"text"))textTest();
    if(all||!strcmp(argv[1],"geometry"))geometryTest(argv[2]);
    return 0;
}
