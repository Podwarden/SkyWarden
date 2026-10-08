#include "physics.h"

PhysicsConfig physics_default_config(void) {
    PhysicsConfig c;
    c.gravity       = 0.20f;
    c.lift          = 0.42f;
    c.v_drag        = 0.90f;
    c.burn_rate     = 0.011f;
    c.cool_rate     = 0.0085f;
    c.density_start = 0.50f;
    c.density_floor = 0.48f;
    c.top_y         = 20.0f;
    c.ground_y      = 200.0f;
    return c;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float physics_alt_frac(const PhysicsConfig* c, float y) {
    float f = (c->ground_y - y) / (c->ground_y - c->top_y);
    return clampf(f, 0.0f, 1.0f);
}

float physics_air_density(const PhysicsConfig* c, float alt_frac) {
    if (alt_frac < c->density_start) return 1.0f;
    float t = (alt_frac - c->density_start) / (1.0f - c->density_start);
    float d = 1.0f - t * (1.0f - c->density_floor);
    return d < c->density_floor ? c->density_floor : d;
}

void physics_vertical_step(const PhysicsConfig* c, VerticalState* s, int burner_on) {
    s->heat += burner_on ? c->burn_rate : -c->cool_rate;
    s->heat = clampf(s->heat, 0.0f, 1.0f);

    float af   = physics_alt_frac(c, s->y);
    float dens = physics_air_density(c, af);
    float net  = c->gravity - s->heat * c->lift * dens; /* <0 => rises */

    s->vy += net;
    s->vy *= c->v_drag;
    s->y  += s->vy;

    if (s->y < c->top_y)    { s->y = c->top_y;    s->vy = 0.0f; }
    if (s->y > c->ground_y) { s->y = c->ground_y; s->vy = 0.0f; }
}
