#include "greatest.h"
#include "tutorial.h"

TEST wind_only_in_band(void) {
    ASSERT_IN_RANGE(0.0f, tutorial_wind_target(40.0f), 1e-6f);
    ASSERT_IN_RANGE(0.0f, tutorial_wind_target(120.0f), 1e-6f);
    ASSERT(tutorial_wind_target(80.0f) > 0.0f);
    ASSERT_IN_RANGE(TUT_WIND_SPEED, tutorial_wind_target(80.0f), 1e-6f);
    ASSERT(tutorial_wind_target(TUT_WINDY_Y0) > 0.0f);
    ASSERT(tutorial_wind_target(TUT_WINDY_Y1) > 0.0f);
    PASS();
}
TEST plaque_reach(void) {
    ASSERT_EQ(1, tutorial_reached_plaque(324.0f, 40.0f));
    ASSERT_EQ(0, tutorial_reached_plaque(100.0f, 180.0f));
    ASSERT_EQ(0, tutorial_reached_plaque(324.0f, 120.0f));
    ASSERT_EQ(1, tutorial_reached_plaque(TUT_PLAQUE_X0, TUT_PLAQUE_Y0));
    PASS();
}
GREATEST_MAIN_DEFS();
int main(int argc, char** argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(wind_only_in_band);
    RUN_TEST(plaque_reach);
    GREATEST_MAIN_END();
}
