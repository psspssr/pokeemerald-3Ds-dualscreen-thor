#ifndef ANDROID_BOTTOM_MENUS_H
#define ANDROID_BOTTOM_MENUS_H

#include <ctr_bottom_content.h>
#include "3ds_video.h"

/* Keep the whitelist next to the shared inverse mapping. PokéNav bands and
 * custom CPU menus already fill their canvas and must never use this crop. */
static inline CtrHostBottomMenuContent AndroidBottomMenu_Content(unsigned screen)
{
    switch(screen) {
    case CTR_CENTRED_STORAGE:
    case CTR_CENTRED_SUMMARY:
    case CTR_CENTRED_BAG_WHOLE:
    case CTR_CENTRED_PARTY_WHOLE:
    case CTR_CENTRED_NAMING:
        return CTR_HOST_BOTTOM_WHOLE;
    case CTR_CENTRED_BAG:
    case CTR_CENTRED_POKEDEX:
    case CTR_CENTRED_PARTY:
        return CTR_HOST_BOTTOM_FIELD;
    default:
        return CTR_HOST_BOTTOM_ORIGINAL;
    }
}

#endif
