#include "greatest.h"
#include "music_map.h"

TEST tempo_curve(void) {
    ASSERT_IN_RANGE(MUSIC_FLOOR_RATIO, music_tempo_ratio(0.0f, 0), 1e-6f);
    ASSERT_IN_RANGE(MUSIC_CEIL_RATIO,  music_tempo_ratio(1.0f, 0), 1e-6f);
    ASSERT(music_tempo_ratio(0.5f,0) > music_tempo_ratio(0.1f,0));   /* monotonic */
    ASSERT(music_tempo_ratio(0.5f,0) >= MUSIC_FLOOR_RATIO);          /* never below floor */
    ASSERT_IN_RANGE(MUSIC_PAD_RATIO,   music_tempo_ratio(0.0f, 1), 1e-6f);  /* on pad */
    ASSERT(music_tempo_ratio(1.0f, 1) < MUSIC_FLOOR_RATIO);          /* pad overrides, below floor */
    PASS();
}
TEST part_bands(void) {
    ASSERT_EQ(0, music_part_index(0.0f));
    ASSERT_EQ(MUSIC_PARTS-1, music_part_index(1.0f));
    ASSERT(music_part_index(0.9f) >= music_part_index(0.1f));        /* non-decreasing */
    PASS();
}
GREATEST_MAIN_DEFS();
int main(int argc,char**argv){GREATEST_MAIN_BEGIN();RUN_TEST(tempo_curve);RUN_TEST(part_bands);GREATEST_MAIN_END();}
