#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "event_object_movement.h"
#include "event_data.h"
#include "fieldmap.h"
#include "overworld.h"
#include "sprite.h"
#include "constants/event_objects.h"
#include "constants/battle_pyramid.h"
#include "constants/trainer_types.h"
#include "3ds_field_view.h"
#undef NDEBUG
#include <assert.h>

#define OBJ_EVENT_GFX_FOLLOWER 0xFF

_Static_assert(OBJECT_EVENTS_COUNT == 16, "keep the original bounded object pool");

static CtrFieldView view;
static unsigned viewGeneration;
struct ObjectEvent gObjectEvents[OBJECT_EVENTS_COUNT];
struct Sprite gSprites[MAX_SPRITES + 1];
struct Camera gCamera;
struct LinkPlayerObjectEvent gLinkPlayerObjectEvents[4];
static struct SaveBlock1 save;
struct SaveBlock1 *gSaveBlock1Ptr = &save;
struct MapHeader gMapHeader;
static struct MapEvents events;
static bool8 hiddenFlags[NUM_FLAG_BYTES * 8], pyramid, hill;
static u8 pyramidCount;
static unsigned created, destroyed;
static const u8 harmlessScript[] = {0};
static const struct ObjectEventGraphicsInfo graphics = {.size = 512, .width = 16, .height = 32, .paletteSlot = PALSLOT_NPC_1};

bool8 FlagGet(u16 id) { assert(id < ARRAY_COUNT(hiddenFlags)); return hiddenFlags[id]; }
u8 VarGetObjectEventGraphicsId(u8 id) { return id + 4; }
static u8 CurrentBattlePyramidLocation(void) { return pyramid ? 1 : PYRAMID_LOCATION_NONE; }
static u8 GetNumBattlePyramidObjectEvents(void) { return pyramidCount; }
static bool8 InTrainerHill(void) { return hill; }
const struct ObjectEventGraphicsInfo *GetObjectEventGraphicsInfo(u8 id) { return &graphics; }
static void MakeSpriteTemplateFromObjectEventTemplate(const struct ObjectEventTemplate *object,
    struct SpriteTemplate *sprite, const struct SubspriteTable **subsprites)
{ memset(sprite, 0, sizeof(*sprite)); *subsprites = NULL; }
void LoadPlayerObjectReflectionPalette(u16 tag, u8 slot) {}
void LoadSpecialObjectReflectionPalette(u16 tag, u8 slot) {}
static void _PatchObjectPalette(u16 tag, u8 slot) {}
u8 CreateSprite(const struct SpriteTemplate *template, s16 x, s16 y, u8 subpriority)
{
    for (unsigned i = 0; i < MAX_SPRITES; i++)
        if (!gSprites[i].inUse)
        {
            memset(&gSprites[i], 0, sizeof(gSprites[i]));
            gSprites[i].inUse = TRUE;
            created++;
            return i;
        }
    return MAX_SPRITES;
}
void DestroySprite(struct Sprite *sprite)
{ assert(sprite->inUse); memset(sprite, 0, sizeof(*sprite)); destroyed++; }
void GetMapCoordsFromSpritePos(s16 x, s16 y, s16 *destX, s16 *destY) { *destX = x * 16; *destY = y * 16; }
void StartSpriteAnim(struct Sprite *sprite, u8 anim) { sprite->animNum = anim; }
u8 GetFaceDirectionAnimNum(u8 direction) { return direction; }
void SetObjectSubpriorityByElevation(u8 elevation, struct Sprite *sprite, u8 subpriority) {}
static void UpdateObjectEventVisibility(struct ObjectEvent *object, struct Sprite *sprite) {}
void SetSubspriteTables(struct Sprite *sprite, const struct SubspriteTable *tables) {}
void FollowerMon_BindSprite(struct Sprite *sprite) {}
void FollowerMon_CameraShift(s16 dx, s16 dy) {}
void VisibleWild_CameraShift(s16 dx, s16 dy) {}

const CtrFieldView *CtrFieldView_Current(void) { return &view; }
unsigned CtrFieldView_Generation(void) { return viewGeneration; }

#include "objects_defs.inc"
#include "objects_helpers.inc"

static void setup(void)
{
    memset(gObjectEvents, 0, sizeof(gObjectEvents));
    memset(gSprites, 0, sizeof(gSprites));
    memset(gLinkPlayerObjectEvents, 0, sizeof(gLinkPlayerObjectEvents));
    memset(&save, 0, sizeof(save)); memset(&gCamera, 0, sizeof(gCamera));
    memset(&gMapHeader, 0, sizeof(gMapHeader)); memset(&events, 0, sizeof(events));
    memset(hiddenFlags, 0, sizeof(hiddenFlags));
    pyramid = hill = FALSE; pyramidCount = 0; created = destroyed = 0;
    save.pos.x = save.pos.y = 100;
    save.location.mapNum = 2; save.location.mapGroup = 3;
    gMapHeader.events = &events;
    if (CTR_VOXEL_ENABLED)
        CtrFieldView_Voxel(&view, 40.0f, 100.0f);
    else
        CtrFieldView_Flat(&view);
    viewGeneration++;
}

static void existing(unsigned slot, u8 localId, s16 x, s16 y)
{
    struct ObjectEvent *object = &gObjectEvents[slot];
    ClearObjectEvent(object);
    object->active = TRUE; object->localId = localId; object->mapNum = 2; object->mapGroup = 3;
    object->initialCoords = object->currentCoords = object->previousCoords = (struct Coords16){x, y};
    object->spriteId = slot; object->graphicsId = 4;
    object->heldMovementActive = TRUE; object->frozen = TRUE;
    object->trainerType = TRAINER_TYPE_NORMAL; object->trainerRange_berryTreeId = 5;
    gSprites[slot].inUse = TRUE;
}

static void template(unsigned index, u8 localId, s16 x, s16 y, u16 flag)
{
    save.objectEventTemplates[index] = (struct ObjectEventTemplate){
        .localId = localId, .graphicsId = 4, .x = x - MAP_OFFSET, .y = y - MAP_OFFSET,
        .movementType = MOVEMENT_TYPE_WANDER_AROUND, .elevation = 2,
        .trainerType = TRAINER_TYPE_NORMAL, .trainerRange_berryTreeId = 4,
        .flagId = flag, .script = harmlessScript,
    };
    if (events.objectEventCount <= index) events.objectEventCount = index + 1;
}

static int find(u8 localId)
{
    for (unsigned i = 0; i < OBJECT_EVENTS_COUNT; i++)
        if (gObjectEvents[i].active && gObjectEvents[i].localId == localId) return i;
    return -1;
}

static unsigned count(void)
{
    unsigned active = 0;
    for (unsigned i = 0; i < OBJECT_EVENTS_COUNT; i++) active += gObjectEvents[i].active;
    return active;
}

static void sameStepRemovalBeforeSpawn(void)
{
    s16 left, right, top, bottom;
    setup();
    GetObjectEventWindow(&left, &right, &top, &bottom);
    for (unsigned i = 0; i < OBJECT_EVENTS_COUNT; i++) existing(i, i + 1, 107, 107);
    gObjectEvents[0].isPlayer = TRUE;
    gLinkPlayerObjectEvents[0] = (struct LinkPlayerObjectEvent){.active = TRUE, .objEventId = 1};
    existing(15, 16, left, 107);
    template(0, 16, left, 107, 0);
    template(1, 50, right + 1, 107, 0);
    struct ObjectEvent retained[15]; memcpy(retained, gObjectEvents, sizeof(retained));
    save.pos.x++;
    UpdateObjectEventsForCameraUpdate(1, 0);
    assert(count() == 16 && find(50) == 15 && find(16) < 0 && destroyed == 1 && created == 1);
    assert(memcmp(retained, gObjectEvents, sizeof(retained)) == 0);
    struct ObjectEvent *incoming = &gObjectEvents[15];
    assert(incoming->trainerType == TRAINER_TYPE_NORMAL && incoming->trainerRange_berryTreeId == 4);
    assert(incoming->range.rangeX == 1 && incoming->range.rangeY == 1);
    assert(!incoming->heldMovementActive && !incoming->frozen);
    assert(incoming->initialCoords.x == right + 1 && incoming->currentElevation == 2);
    for (unsigned cycle = 0; cycle < 4; cycle++)
    {
        save.pos.x = 100; UpdateObjectEventsForCameraUpdate(-1, 0);
        assert(count() == 16 && find(16) >= 0 && find(50) < 0);
        save.pos.x = 101; UpdateObjectEventsForCameraUpdate(1, 0);
        assert(count() == 16 && find(50) >= 0 && find(16) < 0);
        assert(memcmp(retained, gObjectEvents, sizeof(retained)) == 0);
    }
    assert(created == destroyed);
    puts("PASS same-step removal before spawn reuses off-view slots without delaying the incoming NPC");
}

static void flagsAndBounds(void)
{
    s16 left, right, top, bottom;
    setup();
    GetObjectEventWindow(&left, &right, &top, &bottom);
    template(0, 20, left, top, 0);
    template(1, 21, right, bottom, 0);
    template(2, 22, left - 1, 107, 0);
    template(3, 23, right + 1, 107, 0);
    template(4, 24, 107, top - 1, 0);
    template(5, 25, 107, bottom + 1, 0);
    template(6, 26, 107, 107, 123); hiddenFlags[123] = TRUE;
    UpdateObjectEventsForCameraUpdate(0, 0);
    assert(count() == 2 && find(20) >= 0 && find(21) >= 0);
    for (u8 id = 22; id <= 26; id++) assert(find(id) < 0);
    assert(hiddenFlags[123]);
    UpdateObjectEventsForCameraUpdate(0, 0);
    assert(count() == 2 && created == 2 && destroyed == 0);
    hiddenFlags[123] = FALSE;
    UpdateObjectEventsForCameraUpdate(0, 0);
    assert(count() == 3 && find(26) >= 0);
    gMapHeader.events = NULL;
    UpdateObjectEventsForCameraUpdate(0, 0);
    assert(count() == 3 && created == 3);
    puts("PASS real flat/voxel window bounds, script hide flags, duplicate identity and absent-map guards");
}

static void evictionProtectionAndScore(void)
{
    s16 left, right, top, bottom;
    setup();
    GetObjectEventWindow(&left, &right, &top, &bottom);
    for (unsigned i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        existing(i, i + 1, 107, 107);
        gObjectEvents[i].heldMovementActive = gObjectEvents[i].frozen = FALSE;
        template(i, i + 1, 107, 107, 0);
    }
    gObjectEvents[0].isPlayer = TRUE;
    gLinkPlayerObjectEvents[0] = (struct LinkPlayerObjectEvent){.active = TRUE, .objEventId = 1};
    gObjectEvents[2].frozen = TRUE;
    gObjectEvents[3].heldMovementActive = TRUE;
    for (unsigned i = 0; i <= 4; i++)
        gObjectEvents[i].initialCoords = gObjectEvents[i].currentCoords = (struct Coords16){right, bottom};
    template(16, 50, 107, 107, 0);
    TrySpawnObjectEvents(0, 0);
    assert(count() == 16 && find(50) == 4 && find(5) < 0 && created == 1 && destroyed == 1);
    for (unsigned i = 0; i <= 3; i++) assert(gObjectEvents[i].active);

    setup();
    GetObjectEventWindow(&left, &right, &top, &bottom);
    for (unsigned i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        existing(i, i + 1, 107, 107);
        gObjectEvents[i].heldMovementActive = gObjectEvents[i].frozen = FALSE;
        template(i, i + 1, 107, 107, 0);
    }
    gObjectEvents[4].initialCoords = gObjectEvents[4].currentCoords = (struct Coords16){108, 107};
    assert(!EvictFurthestObjectEvent(ObjectEventViewScore(107, 107), left, right, top, bottom));
    assert(count() == 16 && destroyed == 0);
    gObjectEvents[4].initialCoords = gObjectEvents[4].currentCoords = (struct Coords16){right, bottom};
    assert(EvictFurthestObjectEvent(ObjectEventViewScore(107, 107), left, right, top, bottom));
    assert(!gObjectEvents[4].active && count() == 15 && destroyed == 1);

    setup();
    GetObjectEventWindow(&left, &right, &top, &bottom);
    existing(0, 90, 107, 107);
    gObjectEvents[0].heldMovementActive = gObjectEvents[0].frozen = FALSE;
    assert(!CanEvictObjectEvent(0, left, right, top, bottom));
    gObjectEvents[0].initialCoords = gObjectEvents[0].currentCoords = (struct Coords16){left - 1, 107};
    assert(CanEvictObjectEvent(0, left, right, top, bottom));
    puts("PASS eviction protects player/link/frozen/held objects, picks the furthest and honors the anti-flicker gap");
}

static void cameraShiftAndCapacity(void)
{
    s16 left, right, top, bottom;
    setup();
    GetObjectEventWindow(&left, &right, &top, &bottom);
    for (unsigned i = 0; i < OBJECT_EVENTS_COUNT; i++) existing(i, i + 1, 107, 107);
    template(0, 50, 107, 107, 0);
    UpdateObjectEventsForCameraUpdate(0, 0);
    assert(find(50) < 0 && count() == 16 && created == 0 && destroyed == 0);
    gCamera.active = TRUE; gCamera.x = 5; gCamera.y = -3;
    existing(15, 16, left + 4, 107 - 3);
    struct ObjectEvent retained = gObjectEvents[2];
    UpdateObjectEventsForCameraUpdate(0, 0);
    retained.initialCoords.x -= 5; retained.currentCoords.x -= 5; retained.previousCoords.x -= 5;
    retained.initialCoords.y += 3; retained.currentCoords.y += 3; retained.previousCoords.y += 3;
    assert(memcmp(&retained, &gObjectEvents[2], sizeof(retained)) == 0);
    assert(find(50) == 15 && count() == 16 && created == 1 && destroyed == 1);
    setup();
    for (unsigned i = 0; i < MAX_SPRITES; i++) gSprites[i].inUse = TRUE;
    template(0, 50, 107, 107, 0);
    UpdateObjectEventsForCameraUpdate(0, 0);
    assert(count() == 0 && find(50) < 0);
    puts("PASS camera-relative shifts precede retention/spawn; true event/sprite exhaustion still fails safely");
}

static void stillCameraViewChange(void)
{
    setup();
    template(0, 20, 107, 107, 0);
    UpdateObjectEventsForViewChange();
    assert(count() == 1 && find(20) >= 0 && created == 1);
    view.shiftX = 20;
    viewGeneration++;
    template(1, 21, 127, 107, 0);
    UpdateObjectEventsForViewChange();
    assert(count() == 1 && find(20) < 0 && find(21) >= 0 && created == 2 && destroyed == 1);
    puts("PASS still-camera view changes also remove before spawning");
}

static void specialMapCounts(void)
{
    for (unsigned mode = 0; mode < 2; mode++)
    {
        setup();
        for (unsigned i = 0; i < 5; i++) template(i, 30 + i, 107 + i, 107, 0);
        pyramid = mode == 0; hill = mode == 1; pyramidCount = 3;
        UpdateObjectEventsForCameraUpdate(0, 0);
        assert(count() == (pyramid ? pyramidCount : HILL_TRAINERS_PER_FLOOR));
    }
    puts("PASS Battle Pyramid and Trainer Hill keep their existing template-count rules");
}

int main(void)
{
    sameStepRemovalBeforeSpawn(); flagsAndBounds(); evictionProtectionAndScore();
    cameraShiftAndCapacity(); stillCameraViewChange(); specialMapCounts();
    printf("PASS object spawn ordering, CTR_VOXEL_ENABLED=%d (both live renderer modes use this shared game path)\n", CTR_VOXEL_ENABLED);
    return 0;
}
