#include "greatest.h"
#include "audio_util.h"

TEST silent_at_rest(void) {
    ASSERT_IN_RANGE(0.0f, audio_burner_level(0.0f), 1e-6f);
    PASS();
}

TEST rises_with_speed(void) {
    float a = audio_burner_level(5.0f);
    float b = audio_burner_level(15.0f);
    ASSERT(a > 0.0f);
    ASSERT(b > a);                       /* monotonic increasing over the ramp */
    PASS();
}

TEST clamps_to_ceiling(void) {
    ASSERT(audio_burner_level(BURNER_CRANK_FULL)      <= BURNER_MAX + 1e-6f);
    ASSERT(audio_burner_level(BURNER_CRANK_FULL * 4)  <= BURNER_MAX + 1e-6f);
    /* reaches (about) the ceiling once at/over full crank */
    ASSERT(audio_burner_level(BURNER_CRANK_FULL * 4)  >= BURNER_MAX - 1e-6f);
    PASS();
}

TEST direction_agnostic(void) {
    /* cranking backwards is just as much burner as forwards */
    ASSERT_IN_RANGE(audio_burner_level(12.0f), audio_burner_level(-12.0f), 1e-6f);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(silent_at_rest);
    RUN_TEST(rises_with_speed);
    RUN_TEST(clamps_to_ceiling);
    RUN_TEST(direction_agnostic);
    GREATEST_MAIN_END();
}
