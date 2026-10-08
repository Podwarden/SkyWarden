#ifndef BALLY_ENEMIES_H
#define BALLY_ENEMIES_H

#define MAX_MISSILES 24
#define MAX_GUNS     8

typedef struct {
    float x;          /* world-X on the pavement */
    int   moving;     /* 0 = stationary, 1 = patrols */
    float vx;         /* patrol speed (moving guns) */
    float lo, hi;     /* patrol bounds (world-X) */
    int   timer;      /* counts up to cooldown */
    int   cooldown;   /* frames between shots */
} Gun;

typedef struct {
    float x, y, vy;   /* world position; vy < 0 = rising */
    int   alive;
} Missile;

/* Advance a gun; returns 1 on the frame it fires (and resets its timer).
 * `moving` guns also patrol between lo..hi (bounces). */
int  gun_step(Gun* g, float world_w);

/* Advance a missile (rises). Sets alive=0 once it passes above `top_y`. */
void missile_step(Missile* m, float top_y);

/* Wrap-aware hit test: is (mx,my) within radius r of balloon (bx,by)? */
int  missile_hits(float mx, float my, float bx, float by, float r, float world_w);

#endif
