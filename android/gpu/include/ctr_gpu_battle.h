#pragma once
#include <citro3d.h>

/* The battle/transition pixel-art surface follows Android's Sharp/Smooth
 * choice. Terrain MSAA and deliberate bloom/blur/shadow samplers are separate. */
GPU_TEXTURE_FILTER_PARAM CtrGpu_BattleSceneFilter(void);
