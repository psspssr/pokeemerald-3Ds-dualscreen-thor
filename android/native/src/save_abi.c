/* The Android port deliberately shares Emerald's raw GBA flash image.
 * Compile against origin's actual game types, with the game's compiler
 * flags. Stop an upstream update that would silently change that format. */
#include "global.h"
#include "save.h"
#include "pokemon_storage_system.h"
#include <stddef.h>

#define SAVE_OFFSET(type, member, expected) \
    _Static_assert(offsetof(struct type, member) == (expected), #type "." #member " changed GBA offset")

_Static_assert(sizeof(void *) == 4, "the game data ABI requires 32-bit pointers");
_Static_assert(sizeof(struct SaveSector) == 0x1000, "GBA flash sector must be 4 KiB");
_Static_assert(SECTORS_COUNT * sizeof(struct SaveSector) == 128 * 1024, "GBA flash must be 128 KiB");
_Static_assert(SECTOR_SIGNATURE == 0x08012025, "GBA save signature changed");
SAVE_OFFSET(SaveSector, id, 0xFF4);
SAVE_OFFSET(SaveSector, checksum, 0xFF6);
SAVE_OFFSET(SaveSector, signature, 0xFF8);
SAVE_OFFSET(SaveSector, counter, 0xFFC);
_Static_assert(sizeof(struct SaveBlock2) == 0xF2C, "GBA trainer block changed");
_Static_assert(sizeof(struct SaveBlock1) == 0x3D88, "GBA world block changed");
_Static_assert(sizeof(struct PokemonStorage) == 0x83D0, "GBA PC storage changed");
_Static_assert(sizeof(struct BoxPokemon) == 80, "GBA boxed Pokemon changed");
_Static_assert(sizeof(struct Pokemon) == 100, "GBA party Pokemon changed");
SAVE_OFFSET(BoxPokemon, checksum, 28);
SAVE_OFFSET(BoxPokemon, secure, 32);
SAVE_OFFSET(PokemonStorage, boxes, 4);
SAVE_OFFSET(PokemonStorage, boxNames, 0x8344);
SAVE_OFFSET(SaveBlock2, playerName, 0x00);
SAVE_OFFSET(SaveBlock2, playerGender, 0x08);
SAVE_OFFSET(SaveBlock2, playerTrainerId, 0x0A);
SAVE_OFFSET(SaveBlock2, playTimeHours, 0x0E);
SAVE_OFFSET(SaveBlock2, pokedex, 0x18);
SAVE_OFFSET(SaveBlock2, encryptionKey, 0xAC);
SAVE_OFFSET(SaveBlock1, pos, 0x00);
SAVE_OFFSET(SaveBlock1, location, 0x04);
SAVE_OFFSET(SaveBlock1, mapLayoutId, 0x32);
SAVE_OFFSET(SaveBlock1, playerPartyCount, 0x234);
SAVE_OFFSET(SaveBlock1, playerParty, 0x238);
SAVE_OFFSET(SaveBlock1, money, 0x490);
SAVE_OFFSET(SaveBlock1, bagPocket_Items, 0x560);
SAVE_OFFSET(SaveBlock1, flags, 0x1270);
SAVE_OFFSET(SaveBlock1, vars, 0x139C);
SAVE_OFFSET(SaveBlock1, gameStats, 0x159C);
SAVE_OFFSET(SaveBlock1, daycare, 0x3030);
SAVE_OFFSET(SaveBlock1, mysteryGift, 0x322C);
SAVE_OFFSET(SaveBlock1, trainerHill, 0x3D64);
