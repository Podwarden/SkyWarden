#include "greatest.h"
#include "nav.h"
#include <math.h>

#define PI 3.14159265358979f

TEST angle_right_is_zero(void) {
    float a = nav_angle_to(100.0f, 120.0f, 160.0f, 120.0f, 1200.0f);
    ASSERT_IN_RANGE(0.0f, a, 1e-3f);
    PASS();
}

TEST angle_left_is_pi(void) {
    float a = nav_angle_to(160.0f, 120.0f, 100.0f, 120.0f, 1200.0f);
    ASSERT_IN_RANGE(PI, fabsf(a), 1e-3f);
    PASS();
}

TEST angle_below_is_half_pi(void) {
    float a = nav_angle_to(100.0f, 50.0f, 100.0f, 200.0f, 1200.0f);
    ASSERT_IN_RANGE(PI * 0.5f, a, 1e-3f);
    PASS();
}

TEST angle_is_wrap_aware(void) {
    /* balloon x=1190, tower x=10 on a 1200 world: shortest dx=+20 (right), not -1180 */
    float a = nav_angle_to(1190.0f, 120.0f, 10.0f, 120.0f, 1200.0f);
    ASSERT_IN_RANGE(0.0f, a, 1e-3f);
    PASS();
}

TEST centered_within_tol(void) {
    ASSERT_EQ(1, nav_is_centered(100.0f, 110.0f, 1200.0f, 16.0f)); /* dx=10 <=16 */
    ASSERT_EQ(0, nav_is_centered(100.0f, 130.0f, 1200.0f, 16.0f)); /* dx=30 >16 */
    PASS();
}

TEST centered_is_wrap_aware(void) {
    ASSERT_EQ(1, nav_is_centered(1195.0f, 5.0f, 1200.0f, 16.0f)); /* wrap dx=10 */
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(angle_right_is_zero);
    RUN_TEST(angle_left_is_pi);
    RUN_TEST(angle_below_is_half_pi);
    RUN_TEST(angle_is_wrap_aware);
    RUN_TEST(centered_within_tol);
    RUN_TEST(centered_is_wrap_aware);
    GREATEST_MAIN_END();
}
