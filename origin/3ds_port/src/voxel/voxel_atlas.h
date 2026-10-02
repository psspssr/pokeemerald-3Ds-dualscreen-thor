/*
 * Metatile atlas for the voxel overworld.
 *
 * One texture per tileset pair, holding 16x16 pixels per metatile, composed
 * exactly as the reference renderer composes them (two layers of four 8x8
 * subtiles, layer 0 opaque, layer 1 keyed on colour index 0). Derived from
 * BuildVoxelAtlasForHeader in the MIT-licensed voxel_renderer.c of
 * pokeemerald-multiplatform; see NOTICE.md.
 *
 * Unlike the reference, slots are *packed*: the reference gives every one of
 * the 1024 metatile ids a fixed 16x16 cell of a 512x512 texture, which costs
 * 512 KiB whether the map uses 30 metatiles or 900. A 3DS has 8 MiB of linear
 * memory of which the 2D compositor already owns most, and Citro3D flushes the
 * whole heap every frame, so that page rate is not affordable. Here the ids a
 * map actually references are assigned consecutive slots in a 512x256 texture:
 * 256 KiB for up to 512 distinct metatiles, which is more than any Emerald map
 * uses. The id-to-slot table travels with the atlas.
 *
 * The destination is written in PICA200 texture order, so the caller hands in
 * the raw pixel buffer of a C3D_Tex and nothing here needs libctru.
 */
#ifndef CTR_VOXEL_ATLAS_H
#define CTR_VOXEL_ATLAS_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel_world.h"

#define VOXEL_ATLAS_W       512u
#define VOXEL_ATLAS_H       256u
#define VOXEL_ATLAS_SLOT    16u
#define VOXEL_ATLAS_COLUMNS (VOXEL_ATLAS_W / VOXEL_ATLAS_SLOT)
#define VOXEL_ATLAS_ROWS    (VOXEL_ATLAS_H / VOXEL_ATLAS_SLOT)
#define VOXEL_ATLAS_MAX_SLOTS (VOXEL_ATLAS_COLUMNS * VOXEL_ATLAS_ROWS)
#define VOXEL_ATLAS_PIXELS  (VOXEL_ATLAS_W * VOXEL_ATLAS_H)

/*
 * The reference draws furniture legs, table sides and television bodies as
 * untextured `glColor3f` geometry. This shader always samples texture0, so
 * rather than add a per-vertex colour - eight more bytes on every vertex, in a
 * build already short of linear memory - the last few atlas slots hold those
 * flat colours. A "solid" face is then an ordinary textured face whose UVs
 * point at one of them.
 */
typedef enum
{
    VOXEL_SOLID_WOOD = 0,   /* table sides */
    VOXEL_SOLID_WOOD_DARK,  /* table legs */
    VOXEL_SOLID_GREY,       /* television stand */
    VOXEL_SOLID_BLACK,      /* television body */
    VOXEL_SOLID_SHEET,      /* bedding */
    VOXEL_SOLID_COUNT
} VoxelSolidColor;

#define VOXEL_SOLID_SLOTS 8u
#define VOXEL_SOLID_BASE  (VOXEL_ATLAS_MAX_SLOTS - VOXEL_SOLID_SLOTS)

/* Texture coordinates of one of the flat colours. */
void VoxelAtlas_SolidUV(VoxelSolidColor color, float *u0, float *v0, float *u1, float *v1);

/*
 * One entry per metatile id. 0 means this atlas has never seen the id, which
 * is the renderer's cue that a script placed something new and the atlas needs
 * rebuilding once. VOXEL_SLOT_ABSENT means it was seen and has no pixels.
 * Anything else is slot + 1.
 */
#define VOXEL_METATILE_IDS 1024u
#define VOXEL_SLOT_ABSENT  0xFFFFu

typedef struct VoxelAtlasMap
{
    uint16_t slotOf[VOXEL_METATILE_IDS];
    unsigned used;      /* slots actually composed */
    bool overflowed;    /* the map referenced more ids than the atlas holds */
} VoxelAtlasMap;

/*
 * Fills `dest` (VOXEL_ATLAS_PIXELS uint16_t, RGBA5551, PICA order) with the
 * metatiles the active instances reference, and fills `map` with the id-to-slot
 * table. Returns false if nothing could be composed, in which case `dest` is
 * left cleared.
 *
 * With `extend`, `map` is an atlas already in use that has met ids it was not
 * built with: every id keeps its slot and is redrawn there, and only the new
 * ones take free slots - so nothing drawn from the old atlas goes stale.
 */
bool VoxelAtlas_Build(const VoxelMapInstance *inst, uint16_t *dest, VoxelAtlasMap *map,
                      bool extend);

/*
 * The same composition spread over several calls, so that no frame pays for
 * all of it. Begin marks the ids and clears `dest`; each Step decompresses one
 * tileset or composes up to `metatiles` metatiles, and returns true once the
 * job has ended - `ok` then says whether anything was composed, and `map` is
 * the table to install with the upload of `dest`. With `base`, the job extends
 * that table (see VoxelAtlas_Build) rather than starting an empty one.
 *
 * The tileset pair is read afresh at every step; the caller must not change
 * `dest` in between, and cancels the job if the pair stops being wanted.
 */
typedef struct
{
    bool active, extend, ok;
    const void *primary, *secondary;
    int mapGroup, mapNum;
    uint16_t *dest;
    VoxelAtlasMap map;
    uint8_t used[VOXEL_METATILE_IDS];
    uint8_t *primaryTiles, *secondaryTiles;
    bool primaryLoaded, secondaryLoaded;
    unsigned phase, next;
    unsigned added;     /* ids given a slot of their own by this job */
} VoxelAtlasJob;

bool VoxelAtlas_JobBegin(VoxelAtlasJob *job, const VoxelMapInstance *inst, uint16_t *dest,
                         const VoxelAtlasMap *base);
bool VoxelAtlas_JobStep(VoxelAtlasJob *job, unsigned metatiles);
void VoxelAtlas_JobCancel(VoxelAtlasJob *job);
/* Recompose only slots whose source 8x8 tiles were changed in BG_VRAM.
 * dest contains the complete prior atlas and remains valid for unaffected ids. */
unsigned VoxelAtlas_RefreshAnimated(const VoxelMapInstance *inst, const VoxelAtlasMap *map,
                                    uint16_t *dest, const uint8_t *liveTiles,
                                    const uint8_t dirtyTiles[128]);

/* Does this metatile of the instance's tileset pair draw nothing but black -
 * an interior's outside? Known once the pair's atlas has been built. */
bool VoxelAtlas_IsVoid(const VoxelMapInstance *inst, int metatile);

/* Texture coordinates of a packed slot, in the PICA convention where v=1 is
 * the first row in memory. */
void VoxelAtlas_SlotUV(unsigned slot, float *u0, float *v0, float *u1, float *v1);

#endif
