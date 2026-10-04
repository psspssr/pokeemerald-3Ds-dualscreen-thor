#ifndef CTR_BOTTOM_H
#define CTR_BOTTOM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The bottom screen: a touch companion to the game on the top screen (party,
 * region map, bag and trainer card in the field; action and move buttons in
 * battle). See docs/ARCHITECTURE.md.
 *
 * It is drawn by the CPU into a 320x240 canvas laid out exactly like the
 * bottom framebuffer (RGB565, column-major, each column bottom-to-top), and
 * only when what it shows changes. There is no GPU pass, no texture and no
 * VRAM: on an Old 3DS the compositor and the voxel overworld need all of it.
 */
#define CTR_BOTTOM_WIDTH 320
#define CTR_BOTTOM_HEIGHT 240

/* Native side (libctru): copy columns [x0, x1) of the canvas to the screen. */
void CtrBottom_Blit(const uint16_t *canvas, int x0, int x1);
/* Same, restricted to rows [y0, y1) of those columns. */
void CtrBottom_BlitRect(const uint16_t *canvas, int x0, int y0, int x1, int y1);

/* Game side. Init before AgbMain; Frame once per frame, after input is
 * scanned and before the game reads its keys. */
void CtrBottom_Init(void);
void CtrBottom_Frame(void);
/* Keys the bottom screen presses on the player's behalf this frame. */
uint16_t CtrBottom_InjectedKeys(void);

#endif
