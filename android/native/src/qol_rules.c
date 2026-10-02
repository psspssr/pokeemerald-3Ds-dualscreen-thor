#include "android_qol_rules.h"

unsigned CtrQol_ShinyValue(uint32_t trainer, uint32_t personality)
{
    return (trainer ^ (trainer >> 16) ^ personality ^ (personality >> 16)) & 0xffff;
}

static unsigned unownForm(uint32_t personality)
{
    return (((personality >> 18) & 0xc0) | ((personality >> 12) & 0x30)
            | ((personality >> 6) & 0x0c) | (personality & 3)) % 28;
}

uint32_t CtrQol_BoostPersonality(uint32_t original, uint32_t trainer,
                               unsigned multiplier, unsigned constraints)
{
    if (multiplier < 2 || multiplier > 64 || (multiplier & (multiplier - 1)))
        return original;
    unsigned shiny = CtrQol_ShinyValue(trainer, original);
    if (shiny < 8 || shiny >= 8 * multiplier)
        return original;

    /* Qualifying encounters receive a real GBA shiny PID before encryption.
     * Keep the entire gender byte (including ability parity), nature, and
     * species-dependent form/evolution constraints. No RNG draws are added.
     * At most 2048 candidates; incompatible constraints keep the original. */
    for (unsigned delta = 0; delta < 256; ++delta)
    {
        uint32_t low = (original + (delta << 8)) & 0xffff;
        for (unsigned value = 0; value < 8; ++value)
        {
            uint32_t high = ((trainer >> 16) ^ trainer ^ low ^ value) & 0xffff;
            uint32_t candidate = (high << 16) | low;
            if (candidate % 25 != original % 25)
                continue;
            if ((constraints & CTR_QOL_PRESERVE_UNOWN) && unownForm(candidate) != unownForm(original))
                continue;
            if ((constraints & CTR_QOL_PRESERVE_WURMPLE)
                && ((high % 10) < 5) != (((original >> 16) % 10) < 5))
                continue;
            return candidate;
        }
    }
    return original;
}

unsigned CtrQol_BenchExperience(unsigned base, bool enabled, bool participated,
                               bool heldExpShare, bool occupied, bool egg,
                               unsigned hp, unsigned level, bool partner)
{
    if (!enabled || participated || heldExpShare || !occupied || egg || !hp || level >= 100 || partner)
        return 0;
    return base < 2 ? 1 : base / 2;
}
