#ifndef BALLY_BALLOON_H
#define BALLY_BALLOON_H

/* Presentation helpers, pure; no Playdate deps.
 * Imagetable layout: index = dxIndex*5 + state.
 *   dxIndex 0..4  -> basket horizontal offset -2..+2 px (pendulum trail)
 *   state   0     -> burner off (no flame); 1..4 -> burner-on flame frames */
#define BALLOON_FRAME_COLS 5

int balloon_basket_index(float vx);                 /* 0..4 */
int balloon_frame_index(int dx_index, int burner_on, int flame_frame); /* 0..24 */

#endif
