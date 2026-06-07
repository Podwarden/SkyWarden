#include "greatest.h"
#include "camera.h"
#include <math.h>

#define WW 1200.0f
#define SW 400.0f
#define MG 120.0f

static Camera mk(float x) { Camera c = { WW, SW, MG, x }; return c; }

TEST world_wrap_normalizes(void) {
    ASSERT_IN_RANGE(1190.0f, camera_world_wrap(-10.0f, WW), 1e-3f);
    ASSERT_IN_RANGE(5.0f,    camera_world_wrap(1205.0f, WW), 1e-3f);
    ASSERT_IN_RANGE(600.0f,  camera_world_wrap(600.0f, WW), 1e-3f);
    PASS();
}

TEST wrap_delta_shortest(void) {
    ASSERT_IN_RANGE(-10.0f, camera_wrap_delta(1190.0f, WW), 1e-3f);
    ASSERT_IN_RANGE( 10.0f, camera_wrap_delta(-1190.0f, WW), 1e-3f);
    ASSERT_IN_RANGE(-599.0f, camera_wrap_delta(601.0f, WW), 1e-3f);
    PASS();
}

TEST follow_keeps_target_in_deadzone(void) {
    for (float x0 = 0; x0 < WW; x0 += 137.0f) {
        for (float tgt = 0; tgt < WW; tgt += 53.0f) {
            Camera c = mk(camera_world_wrap(x0, WW));
            camera_follow(&c, tgt);
            float sx = camera_screen_x(&c, tgt);
            ASSERT(sx >= MG - 0.5f);
            ASSERT(sx <= SW - MG + 0.5f);
            ASSERT(c.x >= 0.0f && c.x < WW);
        }
    }
    PASS();
}

TEST follow_idle_inside_band(void) {
    Camera c = mk(0.0f);
    camera_follow(&c, 200.0f);
    ASSERT_IN_RANGE(0.0f, c.x, 1e-3f);
    PASS();
}

TEST screen_x_handles_seam(void) {
    Camera c = mk(0.0f);
    ASSERT_IN_RANGE(-10.0f, camera_screen_x(&c, 1190.0f), 1e-3f);
    ASSERT_IN_RANGE(30.0f,  camera_screen_x(&c, 30.0f), 1e-3f);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(world_wrap_normalizes);
    RUN_TEST(wrap_delta_shortest);
    RUN_TEST(follow_keeps_target_in_deadzone);
    RUN_TEST(follow_idle_inside_band);
    RUN_TEST(screen_x_handles_seam);
    GREATEST_MAIN_END();
}
