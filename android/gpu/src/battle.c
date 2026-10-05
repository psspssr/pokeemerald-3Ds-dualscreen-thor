#include "gpu_internal.h"
#include <ctr_gpu_battle.h>

GPU_TEXTURE_FILTER_PARAM CtrGpu_BattleSceneFilter(void)
{
    CtrHostLayout layout;
    CtrHost_GetLayout(&layout);
    return layout.filter ? GPU_LINEAR : GPU_NEAREST;
}
