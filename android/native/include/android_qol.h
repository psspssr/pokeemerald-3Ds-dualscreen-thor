#ifndef ANDROID_QOL_H
#define ANDROID_QOL_H

#include <stdbool.h>
#include <stdint.h>

/* Android-only rules. No fields are added to Emerald's save structures. */
struct Pokemon;
struct BoxPokemon;
void CtrQol_BeginWildMon(struct Pokemon *mon);
void CtrQol_EndWildMon(void);
uint32_t CtrQol_WildPersonality(struct BoxPokemon *mon, uint16_t species,
                              uint32_t personality, unsigned otIdType);
void CtrQol_BeginExperience(unsigned baseExperience);
unsigned CtrQol_ExtraExperience(unsigned partyIndex, bool participated, bool heldExpShare);
bool CtrQol_ConfirmFlee(void);
void CtrQol_ResetMoveEscape(unsigned battler);
bool CtrQol_SelectMoveEscape(unsigned battler, unsigned move);
bool CtrQol_ResolveMoveEscape(unsigned battler);

#endif
