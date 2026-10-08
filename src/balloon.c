#include "balloon.h"
#include <math.h>

int balloon_basket_index(float vx) {
    float av = fabsf(vx);
    int lvl = (av < 0.6f) ? 0 : (av < 1.8f) ? 1 : 2;
    int sgn = (vx > 0.05f) ? 1 : (vx < -0.05f) ? -1 : 0;
    return 2 - sgn * lvl;          /* trails opposite to motion */
}

int balloon_frame_index(int dx_index, int burner_on, int flame_frame) {
    int state = burner_on ? (flame_frame < 0 ? 1 : (flame_frame > 3 ? 4 : flame_frame + 1)) : 0;
    return dx_index * BALLOON_FRAME_COLS + state;
}
