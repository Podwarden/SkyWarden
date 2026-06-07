#include "tower.h"
#include "camera.h"   /* camera_wrap_delta, camera_world_wrap */
#include <math.h>

float tower_cup_y(const Tower* t) {
    return t->pad_y + (t->apex_y - t->pad_y) * t->ext;
}

void tower_launch(Tower* t) {
    if (t->phase == TOWER_DOCKED) t->phase = TOWER_EXTENDING;
}

int tower_step(Tower* t, float world_w) {
    switch (t->phase) {
    case TOWER_EXTENDING:
        t->ext += t->ext_rate;
        if (t->ext >= 1.0f) { t->ext = 1.0f; t->phase = TOWER_RETRACTING; }
        return 0;
    case TOWER_RETRACTING:
        t->ext -= t->ret_rate;
        if (t->ext < 0.0f) t->ext = 0.0f;
        {
            float d = camera_wrap_delta(t->x_land - t->x_launch, world_w);
            t->x = camera_world_wrap(t->x_launch + d * (1.0f - t->ext), world_w);
        }
        if (t->ext <= 0.0f) {
            t->x = camera_world_wrap(t->x_land, world_w);
            t->phase = TOWER_READY;
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

LandResult tower_landing_check(const Tower* t, float bx, float by, float vx, float vy,
                               float world_w, float x_tol, float y_tol, float vmax) {
    if (t->phase != TOWER_READY) return LAND_NONE;
    float dx = camera_wrap_delta(bx - t->x, world_w);
    if (fabsf(dx) > x_tol) return LAND_NONE;
    if (by < t->pad_y - y_tol) return LAND_NONE;
    if (fabsf(vx) <= vmax && fabsf(vy) <= vmax) return LAND_OK;
    return LAND_CRASH;
}
