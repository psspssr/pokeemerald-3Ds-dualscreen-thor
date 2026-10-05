#pragma once
#include <citro3d.h>

/* Android-only terrain MSAA scope. The target must have just been fully
 * cleared and selected with C3D_FrameDrawOn, before any geometry is drawn.
 * Resolve before billboards/UI so their original pixel edges stay sharp.
 * A false begin leaves the ordinary single-sample target/state untouched. */
bool CtrGpu_BeginVoxelAA(C3D_RenderTarget *target);
void CtrGpu_EndVoxelAA(void);

/* Time remaining before the current Android presentation deadline, for
 * optional work in C3D_FrameEndHook. Read-only: does not advance pacing.
 * No budget while unanchored, suspended, exiting or already overdue. */
float CtrGpu_FrameTimeLeftMs(void);

/* Select backing resolution before this frame clears/draws these targets.
 * Public 3DS dimensions/coordinates stay unchanged. Bottom-only menus may
 * reuse the previous top image: keepTop leaves its target/LCD untouched.
 * Allocation/capability failure chooses a lower scale without changing the
 * stored preference. Returns the actual logical-target scale (1..4). */
unsigned CtrGpu_ConfigureVoxelTargets(C3D_RenderTarget *logical,C3D_RenderTarget *top,
                                     bool voxel,bool keepTop);
