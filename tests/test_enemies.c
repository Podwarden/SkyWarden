#include "greatest.h"
#include "enemies.h"

TEST gun_fires_on_cooldown(void) {
    Gun g = {0}; g.x = 100; g.cooldown = 30; g.timer = 0;
    int fires = 0;
    for (int i = 0; i < 30; i++) fires += gun_step(&g, 1200.0f);
    ASSERT_EQ(1, fires);                 /* exactly one shot in 30 frames */
    ASSERT(g.timer < 30);                /* timer reset after firing */
    PASS();
}

TEST moving_gun_patrols_and_bounces(void) {
    Gun g = {0}; g.x = 100; g.moving = 1; g.vx = 5; g.lo = 90; g.hi = 110; g.cooldown = 9999;
    for (int i = 0; i < 100; i++) gun_step(&g, 1200.0f);
    ASSERT(g.x >= 90.0f && g.x <= 110.0f);       /* stays within patrol bounds */
    PASS();
}

TEST missile_rises_and_dies_off_top(void) {
    Missile m = { .x = 100, .y = 200, .vy = -2.0f, .alive = 1 };
    missile_step(&m, 0.0f);
    ASSERT(m.y < 200.0f);                /* rose */
    ASSERT_EQ(1, m.alive);
    m.y = -5.0f; missile_step(&m, 0.0f);
    ASSERT_EQ(0, m.alive);              /* culled above the top */
    PASS();
}

TEST hit_test_near_and_far(void) {
    ASSERT_EQ(1, missile_hits(100, 100, 104, 102, 12.0f, 1200.0f));  /* close */
    ASSERT_EQ(0, missile_hits(100, 100, 140, 100, 12.0f, 1200.0f));  /* far */
    PASS();
}

TEST hit_test_wraps_seam(void) {
    /* missile at x=1198, balloon at x=2 on a 1200 world -> dx=4 (across seam) */
    ASSERT_EQ(1, missile_hits(1198, 100, 2, 101, 12.0f, 1200.0f));
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(gun_fires_on_cooldown);
    RUN_TEST(moving_gun_patrols_and_bounces);
    RUN_TEST(missile_rises_and_dies_off_top);
    RUN_TEST(hit_test_near_and_far);
    RUN_TEST(hit_test_wraps_seam);
    GREATEST_MAIN_END();
}
