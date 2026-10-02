#ifndef CTR_ASSETS_H
#define CTR_ASSETS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Resident index, reclaimable payloads. 24 MiB is a deliberate ARM11 figure:
 * the whole referenced graphics set is smaller than the linear+application heap
 * available here, so the cache exists to bound growth, not to survive on 4 MiB.
 */
#define CTR_ASSET_CACHE_BUDGET (24u * 1024u * 1024u)
/* Never reclaim something the game may still be holding from a recent frame. */
#define CTR_ASSET_CACHE_GRACE_FRAMES 8u

struct CtrAssetStats
{
    uint32_t entries, pointerEntries, indexBytes;
    uint32_t bytes, peakBytes, budget;
    uint32_t loads, hits, misses, evictions, errors;
};

void CtrAssets_SetBudget(uint32_t bytes);
void CtrAssets_Collect(void);
const struct CtrAssetStats *CtrAssets_GetStats(void);
void CtrAssets_Fatal(const char *detail) __attribute__((noreturn));

/* Reserved regions filled from the game data, see 3ds_script_loader.c. */
bool CtrGameData_Init(void);
uint32_t CtrGameData_Bytes(void);
bool CtrScripts_Init(void);
uint32_t CtrScripts_Bytes(void);
bool CtrSongs_Init(void);
uint32_t CtrSongs_Bytes(void);

/* Map payloads: whole-file resident, see 3ds_map_loader.c. */
bool CtrMaps_Init(void);
uint32_t CtrMaps_Bytes(void);

#endif
