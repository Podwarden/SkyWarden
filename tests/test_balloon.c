#include "greatest.h"
#include "balloon.h"

TEST idle_is_centered(void) {
    ASSERT_EQ(2, balloon_basket_index(0.0f));   /* dx 0 => index 2 */
    PASS();
}
TEST fast_right_trails_left(void) {
    /* moving right (vx>0) fast => basket trails LEFT => dx -2 => index 0 */
    ASSERT_EQ(0, balloon_basket_index(2.5f));
    /* medium right => dx -1 => index 1 */
    ASSERT_EQ(1, balloon_basket_index(1.0f));
    PASS();
}
TEST fast_left_trails_right(void) {
    ASSERT_EQ(4, balloon_basket_index(-2.5f)); /* dx +2 */
    ASSERT_EQ(3, balloon_basket_index(-1.0f)); /* dx +1 */
    PASS();
}
TEST frame_index_layout(void) {
    ASSERT_EQ(0,  balloon_frame_index(0, 0, 0));  /* dx0, off */
    ASSERT_EQ(2,  balloon_frame_index(0, 1, 1));  /* dx0, flame_frame 1 -> state 2 */
    ASSERT_EQ(24, balloon_frame_index(4, 1, 4));  /* dx4, flame4 -> 4*5+4 */
    /* burner off ignores flame_frame -> state 0 */
    ASSERT_EQ(10, balloon_frame_index(2, 0, 3));  /* dx2*5 + 0 */
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(idle_is_centered);
    RUN_TEST(fast_right_trails_left);
    RUN_TEST(fast_left_trails_right);
    RUN_TEST(frame_index_layout);
    GREATEST_MAIN_END();
}
