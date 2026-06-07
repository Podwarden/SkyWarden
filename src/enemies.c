#include "enemies.h"
#include "camera.h"   /* camera_wrap_delta */

int gun_step(Gun* g, float world_w) {
    if (g->moving) {
        g->x += g->vx;
        if (g->x < g->lo) { g->x = g->lo; g->vx = -g->vx; }
        if (g->x > g->hi) { g->x = g->hi; g->vx = -g->vx; }
        /* assumes lo >= 0 && hi < world_w (no seam-straddling patrols) */
        if (g->x < 0)         g->x += world_w;
        if (g->x >= world_w)  g->x -= world_w;
    }
    if (++g->timer >= g->cooldown) { g->timer = 0; return 1; }
    return 0;
}

void missile_step(Missile* m, float top_y) {
    if (!m->alive) return;
    m->y += m->vy;                 /* vy < 0 -> rises */
    if (m->y < top_y) m->alive = 0;
}

int missile_hits(float mx, float my, float bx, float by, float r, float world_w) {
    float dx = camera_wrap_delta(mx - bx, world_w);
    float dy = my - by;
    return (dx * dx + dy * dy) <= (r * r);
}
