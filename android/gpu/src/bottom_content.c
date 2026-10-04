#include <ctr_bottom_content.h>

unsigned CtrBottomContent_Regions(CtrHostBottomMenuContent content, CtrHostRect panel,
                                  CtrBottomContentRegion regions[2])
{
    if(panel.w<=0 || panel.h<=0) return 0;
    regions[0]=(CtrBottomContentRegion){{0,0,320,240},panel};
    if(content==CTR_HOST_BOTTOM_WHOLE) {
        regions[0].source=(CtrHostRect){40,40,240,160};
    } else if(content==CTR_HOST_BOTTOM_FIELD) {
        /* Ceil keeps the first sidebar pixel consistent with the app's
         * floor(physicalX*320/panelWidth) touch conversion at odd widths. */
        int width=(int)(((int64_t)panel.w*3+3)/4);
        regions[0].source=(CtrHostRect){0,40,240,160};
        regions[0].destination.w=width;
        regions[1]=(CtrBottomContentRegion){{240,0,80,240},
            {panel.x+width,panel.y,panel.w-width,panel.h}};
        return panel.w>width?2:1;
    }
    return 1;
}

static int scaleFloor(int value,int source,int destination)
{
    int64_t numerator=(int64_t)value*source;
    int result=(int)(numerator/destination);
    return result-(numerator<0 && numerator%destination!=0);
}

void CtrBottomContent_SourcePoint(CtrHostBottomMenuContent content,int *x,int *y)
{
    CtrBottomContentRegion regions[2];
    CtrBottomContent_Regions(content,(CtrHostRect){0,0,320,240},regions);
    CtrBottomContentRegion r=regions[0];
    *x=r.source.x+scaleFloor(*x,r.source.w,r.destination.w);
    *y=r.source.y+scaleFloor(*y,r.source.h,r.destination.h);
}
