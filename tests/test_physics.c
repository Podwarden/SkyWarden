#include "greatest.h"
#include "physics.h"
#include <math.h>

static PhysicsConfig C;

TEST alt_frac_endpoints(void) {
    ASSERT_IN_RANGE(0.0f, physics_alt_frac(&C, C.ground_y), 1e-4f);
    ASSERT_IN_RANGE(1.0f, physics_alt_frac(&C, C.top_y), 1e-4f);
    /* monotonic: higher on screen (smaller y) => larger altFrac */
    float mid = (C.top_y + C.ground_y) * 0.5f;
    ASSERT(physics_alt_frac(&C, mid) > physics_alt_frac(&C, C.ground_y));
    ASSERT(physics_alt_frac(&C, C.top_y) > physics_alt_frac(&C, mid));
    PASS();
}

TEST density_thins_with_altitude(void) {
    ASSERT_IN_RANGE(1.0f, physics_air_density(&C, 0.0f), 1e-4f);      /* full near ground */
    ASSERT_IN_RANGE(1.0f, physics_air_density(&C, C.density_start - 0.01f), 1e-4f);
    float top = physics_air_density(&C, 1.0f);
    ASSERT_IN_RANGE(C.density_floor, top, 1e-4f);                     /* hits floor at top */
    ASSERT(physics_air_density(&C, 0.8f) < physics_air_density(&C, 0.6f)); /* decreasing */
    ASSERT(physics_air_density(&C, 1.0f) >= C.density_floor - 1e-4f); /* never below floor */
    PASS();
}

TEST heat_clamps_and_responds(void) {
    VerticalState s = { .y = C.ground_y, .vy = 0, .heat = 0.5f };
    physics_vertical_step(&C, &s, 1);
    ASSERT(s.heat > 0.5f);                       /* burner on raises heat */
    s.heat = 1.0f; physics_vertical_step(&C, &s, 1);
    ASSERT(s.heat <= 1.0f + 1e-6f);              /* clamps at 1 */
    s.heat = 0.0f; physics_vertical_step(&C, &s, 0);
    ASSERT(s.heat >= 0.0f);                      /* clamps at 0 */
    PASS();
}

TEST rises_hot_sinks_cold(void) {
    VerticalState hot = { .y = C.ground_y, .vy = 0, .heat = 1.0f };
    physics_vertical_step(&C, &hot, 1);
    ASSERT(hot.vy < 0.0f);                        /* hot air rises (vy negative) */
    VerticalState cold = { .y = (C.top_y + C.ground_y) * 0.5f, .vy = 0, .heat = 0.0f };
    physics_vertical_step(&C, &cold, 0);
    ASSERT(cold.vy > 0.0f);                        /* cold sinks */
    PASS();
}

TEST struggles_at_ceiling(void) {
    /* At the very top with full heat, lift barely beats gravity: net force is
     * negative (still rises) but tiny in magnitude -> must struggle to hold. */
    float net = C.gravity - 1.0f * C.lift * physics_air_density(&C, 1.0f);
    ASSERT(net < 0.0f);            /* can still climb */
    ASSERT(net > -0.05f);          /* but only barely */
    PASS();
}

TEST clamps_to_bounds(void) {
    VerticalState s = { .y = C.top_y, .vy = -5.0f, .heat = 1.0f };
    physics_vertical_step(&C, &s, 1);
    ASSERT(s.y >= C.top_y - 1e-4f);
    ASSERT_IN_RANGE(0.0f, s.vy, 1e-4f);           /* velocity zeroed at top */
    s.y = C.ground_y; s.vy = 5.0f; s.heat = 0.0f;
    physics_vertical_step(&C, &s, 0);
    ASSERT(s.y <= C.ground_y + 1e-4f);
    ASSERT_IN_RANGE(0.0f, s.vy, 1e-4f);           /* velocity zeroed at ground */
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv) {
    C = physics_default_config();
    GREATEST_MAIN_BEGIN();
    RUN_TEST(alt_frac_endpoints);
    RUN_TEST(density_thins_with_altitude);
    RUN_TEST(heat_clamps_and_responds);
    RUN_TEST(rises_hot_sinks_cold);
    RUN_TEST(struggles_at_ceiling);
    RUN_TEST(clamps_to_bounds);
    GREATEST_MAIN_END();
}
