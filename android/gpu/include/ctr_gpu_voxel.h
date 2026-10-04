#pragma once
#include <citro3d.h>

/* Android-only terrain MSAA scope. The target must have just been fully
 * cleared and selected with C3D_FrameDrawOn, before any geometry is drawn.
 * Resolve before billboards/UI so their original pixel edges stay sharp.
 * A false begin leaves the ordinary single-sample target/state untouched. */
bool CtrGpu_BeginVoxelAA(C3D_RenderTarget *target);
void CtrGpu_EndVoxelAA(void);
