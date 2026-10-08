#ifndef BALLY_CAMERA_H
#define BALLY_CAMERA_H

/* Horizontal scrolling world: a wrapping cylinder of width world_w.
 * Dead-zone edge-scroll: the camera scrolls only when the followed point reaches
 * `margin` px from a screen edge. Pure; no Playdate deps. y/vertical is unaffected. */
typedef struct {
    float world_w;   /* total world width (positions wrap modulo this) */
    float screen_w;  /* viewport width */
    float margin;    /* dead-zone margin from each screen edge */
    float x;         /* camera left edge in world coords, kept in [0, world_w) */
} Camera;

float camera_world_wrap(float v, float world_w);       /* normalize to [0, world_w) */
float camera_wrap_delta(float d, float world_w);       /* normalize to (-world_w/2, world_w/2] */
void  camera_follow(Camera* c, float target_world_x);  /* dead-zone scroll + wrap */
float camera_screen_x(const Camera* c, float world_x); /* on-screen x = wrapDelta(world_x - cam.x) */

#endif
