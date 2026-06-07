#include "camera.h"
#include <math.h>

float camera_world_wrap(float v, float world_w) {
    v = fmodf(v, world_w);
    if (v < 0.0f) v += world_w;
    return v;
}

float camera_wrap_delta(float d, float world_w) {
    float half = world_w * 0.5f;
    d = fmodf(d, world_w);
    if (d < -half) d += world_w;
    if (d >  half) d -= world_w;
    return d;
}

void camera_follow(Camera* c, float target_world_x) {
    float pos = camera_wrap_delta(target_world_x - c->x, c->world_w); /* target's screen-x */
    if (pos < c->margin) {
        c->x = target_world_x - c->margin;
    } else if (pos > c->screen_w - c->margin) {
        c->x = target_world_x - (c->screen_w - c->margin);
    }
    c->x = camera_world_wrap(c->x, c->world_w);
}

float camera_screen_x(const Camera* c, float world_x) {
    return camera_wrap_delta(world_x - c->x, c->world_w);
}
