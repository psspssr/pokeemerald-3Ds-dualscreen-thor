/*
 * Reader for voxel/regions.bin (game data). See voxel_regions.h and the generator
 * that writes it, scripts/gen_voxel_regions.py, which documents the format.
 */

/* Before global.h, which redefines abs() as a macro over stdlib's prototype. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "port_log.h"

#include "voxel_regions.h"
#include "voxel_file.h"

#define VOXEL_REGIONS_PATH "voxel/regions.bin"

/*
 * Layouts held in memory at once: the current map, up to four connections, and
 * the maps one crossing away that are read ahead (VoxelRegions_ReadDetached).
 * With fewer slots than maps on screen a single chunk build - whose shadow
 * rays cross map edges - evicted and re-read a layout from RomFS several times
 * over. A layout is one byte per cell, so the worst case is well under 100 KiB
 * on the ordinary heap. Linear memory is not touched: nothing here reaches the
 * GPU.
 */
#define VOXEL_REGION_SLOTS 16

struct RegionIndex
{
    uint16_t layoutId;
    uint16_t width, height;
    uint32_t roleOffset;
};

struct RegionSlot
{
    uint16_t layoutId;
    uint16_t width, height;
    uint8_t *roles;
    uint32_t stamp;
};

static struct RegionIndex *sIndex;
static unsigned sIndexCount;
static struct RegionSlot sSlots[VOXEL_REGION_SLOTS];
static struct RegionSlot *sRecent;
static uint32_t sStamp;
static FILE *sFile;
/*
 * Through the default 1 KiB buffer a layout was a RomFS request per kilobyte,
 * each a round trip to the SD card on hardware - the part of a map crossing
 * that no emulator shows. One buffer as large as a layout makes it one.
 */
static char sFileBuffer[16 * 1024];
/* The read-ahead's own file, used only by the page stream's worker. */
static FILE *sAheadFile;
static char sAheadBuffer[16 * 1024];

static void FreeSlot(struct RegionSlot *slot)
{
    free(slot->roles);
    slot->roles = NULL;
    slot->layoutId = 0;
    if (sRecent == slot)
        sRecent = NULL;
}

bool VoxelRegions_Init(void)
{
    uint8_t header[8];

    VoxelRegions_Shutdown();
    sFile = VoxelFile_Open(VOXEL_REGIONS_PATH);
    if (sFile == NULL)
    {
        PORT_LOG("[VIDEO] VOXEL regions: %s absent, no signposts stand\n",
                VOXEL_REGIONS_PATH);
        return false;
    }
    setvbuf(sFile, sFileBuffer, _IOFBF, sizeof(sFileBuffer));
    if (fread(header, 1, sizeof(header), sFile) != sizeof(header)
     || memcmp(header, "VXR5", 4) != 0)
    {
        PORT_LOG("[ERROR] VOXEL regions: bad header\n");
        VoxelRegions_Shutdown();
        return false;
    }
    sIndexCount = (unsigned)header[4] | ((unsigned)header[5] << 8);
    if (sIndexCount == 0)
    {
        VoxelRegions_Shutdown();
        return false;
    }
    sIndex = malloc(sIndexCount * sizeof(*sIndex));
    if (sIndex == NULL)
    {
        PORT_LOG("[ERROR] VOXEL regions: no heap for %u index entries\n", sIndexCount);
        VoxelRegions_Shutdown();
        return false;
    }
    /* The file packs each entry as 12 bytes; the struct is padded, so the
     * index is read field by field rather than in one block. */
    for (unsigned i = 0; i < sIndexCount; ++i)
    {
        uint8_t row[12];

        if (fread(row, 1, sizeof(row), sFile) != sizeof(row))
        {
            PORT_LOG("[ERROR] VOXEL regions: index truncated at %u\n", i);
            VoxelRegions_Shutdown();
            return false;
        }
        sIndex[i].layoutId = (uint16_t)(row[0] | (row[1] << 8));
        sIndex[i].width = (uint16_t)(row[2] | (row[3] << 8));
        sIndex[i].height = (uint16_t)(row[4] | (row[5] << 8));
        sIndex[i].roleOffset = (uint32_t)row[8] | ((uint32_t)row[9] << 8)
                             | ((uint32_t)row[10] << 16) | ((uint32_t)row[11] << 24);
    }
    PORT_LOG("[VIDEO] VOXEL regions: %u layouts indexed\n", sIndexCount);
    return true;
}

void VoxelRegions_Shutdown(void)
{
    for (unsigned i = 0; i < VOXEL_REGION_SLOTS; ++i)
        FreeSlot(&sSlots[i]);
    free(sIndex);
    sIndex = NULL;
    sIndexCount = 0;
    sRecent = NULL;
    if (sFile != NULL)
    {
        fclose(sFile);
        sFile = NULL;
    }
    /* The worker has been stopped by now (CtrVoxel_Shutdown stops it first). */
    if (sAheadFile != NULL)
    {
        fclose(sAheadFile);
        sAheadFile = NULL;
    }
}

/* The index is written in layout order, so it is searched rather than scanned. */
static const struct RegionIndex *FindLayout(unsigned layoutId)
{
    unsigned low = 0, high = sIndexCount;

    while (low < high)
    {
        unsigned mid = (low + high) / 2;

        if (sIndex[mid].layoutId == layoutId)
            return &sIndex[mid];
        if (sIndex[mid].layoutId < layoutId)
            low = mid + 1;
        else
            high = mid;
    }
    return NULL;
}

/*
 * Reads one layout from `file` into `out`, allocating its table. Touches no
 * slot and no shared state but the immutable index, so the page stream's
 * worker may call it with a file of its own (VoxelRegions_ReadDetached).
 */
static bool ReadLayout(FILE *file, const struct RegionIndex *entry, struct RegionSlot *out)
{
    uint32_t cells = (uint32_t)entry->width * entry->height;

    memset(out, 0, sizeof(*out));
    if (file == NULL || cells == 0)
        return false;
    out->roles = malloc(cells);
    if (out->roles == NULL)
    {
        /* Not a theory: the log says whether the heap ran out. */
        PORT_LOG("[ERROR] VOXEL regions: no heap for layout %u (%lu cells)\n",
                entry->layoutId, (unsigned long)cells);
        return false;
    }
    if (fseek(file, (long)entry->roleOffset, SEEK_SET) != 0
     || fread(out->roles, 1, cells, file) != cells)
    {
        PORT_LOG("[ERROR] VOXEL regions: layout %u unreadable\n", entry->layoutId);
        FreeSlot(out);
        return false;
    }
    out->layoutId = entry->layoutId;
    out->width = entry->width;
    out->height = entry->height;
    return true;
}

static struct RegionSlot *Cached(unsigned layoutId)
{
    for (unsigned i = 0; i < VOXEL_REGION_SLOTS; ++i)
        if (sSlots[i].roles != NULL && sSlots[i].layoutId == layoutId)
            return &sSlots[i];
    return NULL;
}

/* Moves a read layout into the least recently used slot. */
static struct RegionSlot *Install(struct RegionSlot *layout)
{
    struct RegionSlot *victim = &sSlots[0];

    for (unsigned i = 0; i < VOXEL_REGION_SLOTS; ++i)
    {
        if (sSlots[i].roles == NULL)
        {
            victim = &sSlots[i];
            break;
        }
        if (sSlots[i].stamp < victim->stamp)
            victim = &sSlots[i];
    }
    FreeSlot(victim);
    *victim = *layout;
    victim->stamp = ++sStamp;
    return victim;
}

static struct RegionSlot *Load(unsigned layoutId)
{
    const struct RegionIndex *entry;
    struct RegionSlot *hit = Cached(layoutId), layout;

    if (hit != NULL)
    {
        hit->stamp = ++sStamp;
        return hit;
    }
    entry = FindLayout(layoutId);
    if (entry == NULL || !ReadLayout(sFile, entry, &layout))
        return NULL;
    return Install(&layout);
}

/*
 * Reading ahead. A layout read here, on the render thread, is a round trip to
 * the SD card in the middle of a frame. The maps one crossing away are known
 * before the crossing, so the renderer asks the page stream's worker to read
 * theirs while the player is still walking towards them, and installs what it
 * read between frames.
 */
bool VoxelRegions_Wanted(unsigned layoutId)
{
    return sIndex != NULL && layoutId != 0 && Cached(layoutId) == NULL
        && FindLayout(layoutId) != NULL;
}

void *VoxelRegions_ReadDetached(unsigned layoutId)
{
    const struct RegionIndex *entry = sIndex != NULL ? FindLayout(layoutId) : NULL;
    struct RegionSlot *layout;

    if (entry == NULL)
        return NULL;
    if (sAheadFile == NULL)
    {
        sAheadFile = VoxelFile_Open(VOXEL_REGIONS_PATH);
        if (sAheadFile == NULL)
            return NULL;
        setvbuf(sAheadFile, sAheadBuffer, _IOFBF, sizeof(sAheadBuffer));
    }
    layout = malloc(sizeof(*layout));
    if (layout == NULL)
        return NULL;
    if (!ReadLayout(sAheadFile, entry, layout))
    {
        free(layout);
        return NULL;
    }
    return layout;
}

void VoxelRegions_Adopt(void *read)
{
    struct RegionSlot *layout = read;

    if (layout == NULL)
        return;
    if (Cached(layout->layoutId) == NULL)
        Install(layout);
    else
        FreeSlot(layout);
    free(layout);
}

/*
 * The rebuild asks about one layout thousands of times in a row, so the slot
 * it last answered from is worth a pointer.
 */
static struct RegionSlot *SlotFor(unsigned layoutId)
{
    struct RegionSlot *slot = sRecent;

    if (sIndex == NULL || layoutId == 0)
        return NULL;
    if (slot == NULL || slot->layoutId != layoutId)
    {
        slot = Load(layoutId);
        if (slot == NULL)
            return NULL;
        sRecent = slot;
    }
    return slot;
}

unsigned VoxelRegions_RoleAt(unsigned layoutId, int localX, int localY)
{
    struct RegionSlot *slot = SlotFor(layoutId);

    if (slot == NULL || localX < 0 || localY < 0
     || localX >= (int)slot->width || localY >= (int)slot->height)
        return VOXEL_ROLE_FLOOR;
    return slot->roles[localY * slot->width + localX];
}
