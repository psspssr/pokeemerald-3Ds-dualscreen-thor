#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "gba/types.h"
#include "gba/defines.h"
#include "sprite.h"
#include "3ds_video.h"
#define CONTESTANT_COUNT 4

struct Sprite gSprites[MAX_SPRITES + 1];
s16 gSpriteCoordOffsetX, gSpriteCoordOffsetY;
static struct {u8 sliderHeartSpriteId; bool8 sliderUpdating;} eContestGfxState[4];
static struct {s16 pointTotal;} eContestantStatus[4];
static u8 gContestantTurnOrder[4];
void SpriteCallbackDummy(struct Sprite *sprite) { (void)sprite; }
static void SpriteCB_UpdateHeartSlider(struct Sprite *sprite);
#include "heart_helpers.inc"

static bool sStage, sCentred = true, sBattle, sTransitionCompose;
static float sOffX, sOffY, sZoom = 1;
#ifdef PORT_BRIDGE
static int sTargetW = CTR_GAME_WIDTH, sTargetH = CTR_GAME_HEIGHT;
static int sViewX = CTR_STAGE_X, sViewY = CTR_STAGE_Y;
#else
static int sTargetW = 240, sTargetH = 160, sViewX, sViewY;
#endif
static int sClipX0, sClipX1, sClipY0, sClipY1;
#include "heart_clip.inc"

static bool onscreen(struct Sprite *sprite)
{
    if (sprite->invisible) return false;
    /* Real OamData bitfields perform the same 9/8-bit narrowing as the game.
     * This heart is 8x8; its affine double-size form has a 16x16 box. */
    unsigned boxW = sprite->oam.affineMode & ST_OAM_AFFINE_DOUBLE_MASK ? 16 : 8;
    unsigned boxH = boxW;
    unsigned attr1 = sprite->oam.x, attr0 = sprite->oam.y;
#include "heart_decode.inc"
    return x < sClipX1 && x + (int)boxW > sClipX0
        && y < sClipY1 && y + (int)boxH > sClipY0;
}

static void setup(unsigned rotation, unsigned affine, unsigned scoreOffset, bool invisible)
{
    memset(gSprites, 0, sizeof(gSprites));
    for (unsigned i=0;i<4;i++) {
        struct Sprite *s = &gSprites[i];
        eContestGfxState[i].sliderHeartSpriteId=i;
        gContestantTurnOrder[i]=(i+rotation)%4;
        s->oam=sOam_SliderHeart; s->oam.affineMode=affine;
        s->inUse=true; s->invisible=invisible; s->x=180; s->x2=scoreOffset;
        s->callback=SpriteCallbackDummy;
        CalcCenterToCornerVec(s,s->oam.shape,s->oam.size,affine);
    }
    UpdateSliderHeartSpriteYPositions();
    UpdateOamCoords();
}

int main(void)
{
    ClipToView();
    const unsigned modes[]={ST_OAM_AFFINE_OFF,ST_OAM_AFFINE_NORMAL,ST_OAM_AFFINE_DOUBLE};
    for(unsigned rotation=0;rotation<4;rotation++)
    for(unsigned m=0;m<3;m++)for(unsigned x2=0;x2<=56;x2++)for(unsigned invisible=0;invisible<2;invisible++) {
        setup(rotation,modes[m],x2,invisible);
        for(unsigned pass=0;pass<2;pass++) {
            SetBottomSliderHeartsInvisibility(true);
            UpdateOamCoords();
            for(unsigned i=0;i<4;i++) {
                struct Sprite *s=&gSprites[i];
                assert(s->invisible==invisible && s->x2==(int)x2 && s->callback==SpriteCallbackDummy);
                assert(s->y==sSliderHeartYPositions[gContestantTurnOrder[i]]);
                if(gContestantTurnOrder[i]>1) {
                    if(onscreen(s)) fprintf(stderr,"leaked hidden heart: order%u x%d x2%d corner%d oam%u view%d..%d\n",gContestantTurnOrder[i],s->x,s->x2,s->centerToCornerVecX,s->oam.x,sClipX0,sClipX1);
                    assert(!onscreen(s));
                } else assert(s->x==180 && onscreen(s)==!invisible);
            }
        }
        SetBottomSliderHeartsInvisibility(false);
        UpdateOamCoords();
        for(unsigned i=0;i<4;i++)assert(gSprites[i].x==180 && gSprites[i].invisible==invisible && onscreen(&gSprites[i])==!invisible);
    }
    /* Score callbacks continue while lower hearts are parked. Their normal
     * show/update/disappear flags remain authoritative across this helper. */
    const s16 totals[]={-100,0,10,270,280,1000};
    for(unsigned n=0;n<sizeof(totals)/sizeof(*totals);n++) {
        setup(0,ST_OAM_AFFINE_NORMAL,56,false);
        SetBottomSliderHeartsInvisibility(true);
        eContestantStatus[3].pointTotal=totals[n];
        UpdateHeartSlider(3);
        for(unsigned f=0;f<60 && eContestGfxState[3].sliderUpdating;f++) {
            gSprites[3].callback(&gSprites[3]); UpdateOamCoords(); assert(!onscreen(&gSprites[3]));
        }
        assert(!eContestGfxState[3].sliderUpdating);
        int expected=totals[n]/10*2; if(expected<0)expected=0; if(expected>56)expected=56;
        assert(gSprites[3].x2==expected);
        gSprites[3].invisible=true; // Existing disappear phase owns this bit.
        SetBottomSliderHeartsInvisibility(false); assert(gSprites[3].invisible);
        gSprites[3].invisible=false; UpdateOamCoords(); assert(onscreen(&gSprites[3]));
    }
    puts("PASS real Contest heart hide/show, score callbacks, centre offsets, OAM narrowing and renderer wrap/clip: every x2=0..56, every position, affine boxes and visibility flags");
    return 0;
}
