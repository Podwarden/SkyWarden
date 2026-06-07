#include "greatest.h"
#include "tower.h"
#include <math.h>

#define WW 1200.0f

static Tower mk(void) {
    Tower t = {0};
    t.x = 80.0f; t.x_launch = 80.0f; t.x_land = 240.0f;
    t.ext = 0.0f; t.phase = TOWER_DOCKED;
    t.pad_y = 178.0f; t.apex_y = 24.0f;
    t.ext_rate = 0.04f; t.ret_rate = 0.03f;
    return t;
}

TEST cup_y_lerps_with_extension(void) {
    Tower t = mk();
    ASSERT_IN_RANGE(t.pad_y, tower_cup_y(&t), 1e-3f);
    t.ext = 1.0f;
    ASSERT_IN_RANGE(t.apex_y, tower_cup_y(&t), 1e-3f);
    t.ext = 0.5f;
    ASSERT_IN_RANGE((t.pad_y + t.apex_y) * 0.5f, tower_cup_y(&t), 1e-3f);
    PASS();
}

TEST launch_runs_full_cycle_to_ready(void) {
    Tower t = mk();
    tower_launch(&t);
    ASSERT_EQ(TOWER_EXTENDING, t.phase);
    int became_ready = 0, guard = 0;
    while (t.phase != TOWER_READY && guard++ < 100000)
        became_ready |= tower_step(&t, WW);
    ASSERT_EQ(TOWER_READY, t.phase);
    ASSERT(became_ready == 1);
    ASSERT_IN_RANGE(0.0f, t.ext, 1e-2f);
    ASSERT_IN_RANGE(t.x_land, t.x, 0.5f);
    PASS();
}

TEST extends_to_apex_before_retracting(void) {
    Tower t = mk(); tower_launch(&t);
    int saw_apex = 0;
    for (int i = 0; i < 100 && t.phase == TOWER_EXTENDING; i++) {
        tower_step(&t, WW);
        if (t.ext >= 0.999f) saw_apex = 1;
    }
    ASSERT(saw_apex == 1);
    ASSERT_EQ(TOWER_RETRACTING, t.phase);
    PASS();
}

TEST landing_ok_when_over_pad_and_slow(void) {
    Tower t = mk(); t.phase = TOWER_READY; t.x = 240.0f;
    LandResult r = tower_landing_check(&t, 242.0f, 178.0f, 0.3f, 0.4f, WW, 14.0f, 6.0f, 1.2f);
    ASSERT_EQ(LAND_OK, r);
    PASS();
}

TEST landing_crash_when_too_fast(void) {
    Tower t = mk(); t.phase = TOWER_READY; t.x = 240.0f;
    LandResult r = tower_landing_check(&t, 240.0f, 178.0f, 2.5f, 0.2f, WW, 14.0f, 6.0f, 1.2f);
    ASSERT_EQ(LAND_CRASH, r);
    PASS();
}

TEST landing_none_when_away_or_high(void) {
    Tower t = mk(); t.phase = TOWER_READY; t.x = 240.0f;
    ASSERT_EQ(LAND_NONE, tower_landing_check(&t, 300.0f, 178.0f, 0.1f, 0.1f, WW, 14.0f, 6.0f, 1.2f));
    ASSERT_EQ(LAND_NONE, tower_landing_check(&t, 240.0f, 120.0f, 0.1f, 0.1f, WW, 14.0f, 6.0f, 1.2f));
    PASS();
}

TEST landing_check_handles_seam(void) {
    Tower t = mk(); t.phase = TOWER_READY; t.x = 5.0f;
    LandResult r = tower_landing_check(&t, 1198.0f, 178.0f, 0.2f, 0.2f, WW, 14.0f, 6.0f, 1.2f);
    ASSERT_EQ(LAND_OK, r);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(cup_y_lerps_with_extension);
    RUN_TEST(launch_runs_full_cycle_to_ready);
    RUN_TEST(extends_to_apex_before_retracting);
    RUN_TEST(landing_ok_when_over_pad_and_slow);
    RUN_TEST(landing_crash_when_too_fast);
    RUN_TEST(landing_none_when_away_or_high);
    RUN_TEST(landing_check_handles_seam);
    GREATEST_MAIN_END();
}
