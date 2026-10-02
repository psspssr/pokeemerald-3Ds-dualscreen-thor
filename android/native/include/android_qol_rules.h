#ifndef ANDROID_QOL_RULES_H
#define ANDROID_QOL_RULES_H

#include <stdbool.h>
#include <stdint.h>

enum { CTR_QOL_PRESERVE_UNOWN = 1, CTR_QOL_PRESERVE_WURMPLE = 2 };
unsigned CtrQol_ShinyValue(uint32_t trainer, uint32_t personality);
uint32_t CtrQol_BoostPersonality(uint32_t personality, uint32_t trainer,
                               unsigned multiplier, unsigned constraints);
unsigned CtrQol_BenchExperience(unsigned base, bool enabled, bool participated,
                               bool heldExpShare, bool occupied, bool egg,
                               unsigned hp, unsigned level, bool partner);

#endif
