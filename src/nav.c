#include "nav.h"
#include "camera.h"   /* camera_wrap_delta */
#include <math.h>

float nav_angle_to(float bx, float by, float tx, float ty, float world_w) {
    float dx = camera_wrap_delta(tx - bx, world_w);   /* wrap-aware shortest */
    float dy = ty - by;                               /* +y is down (screen) */
    return atan2f(dy, dx);
}

int nav_is_centered(float bx, float tx, float world_w, float tol) {
    return fabsf(camera_wrap_delta(tx - bx, world_w)) <= tol;
}
