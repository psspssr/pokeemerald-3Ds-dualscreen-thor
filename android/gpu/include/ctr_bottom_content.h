#ifndef CTR_BOTTOM_CONTENT_H
#define CTR_BOTTOM_CONTENT_H

#include <ctr_host.h>

/* Source rectangles use the original, unmodified320x240 bottom LCD. Only
 * decorative margins around known240x160 game menus are omitted. */
typedef struct
{
    CtrHostRect source, destination;
} CtrBottomContentRegion;

unsigned CtrBottomContent_Regions(CtrHostBottomMenuContent content, CtrHostRect panel,
                                  CtrBottomContentRegion regions[2]);
/* Map the app's virtual320x240 point back into the original LCD. Callers keep
 * their existing GBA-origin subtraction. Drag points remain out of bounds. */
void CtrBottomContent_SourcePoint(CtrHostBottomMenuContent content, int *x, int *y);

#endif
