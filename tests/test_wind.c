#include "greatest.h"
#include "wind.h"
#include <math.h>

/* Two clouded echelons (top: +3 right, bottom: -3 left) + tuning. */
static WindField make_field(void) {
    WindField w; w.count = 2; w.ech_height = 40.0f; w.wind_ease = 0.045f;
    w.center[0] = 100.0f; w.base[0] = 3.0f;
    w.center[1] = 180.0f; w.base[1] = -3.0f;
    for (int i = 0; i < 2; i++) { w.f1[i]=0; w.p1[i]=0; w.f2[i]=0; w.p2[i]=0; }
    return w; /* zero gust freqs => gust(t)=0.5+0.5+0.3*? -> see test uses t s.t. constant */
}

TEST gust_nonnegative(void) {
    WindField w = make_field();
    w.f1[0] = 0.02f; w.f2[0] = 0.05f; w.p1[0] = 1.0f; w.p2[0] = 2.0f;
    for (float t = 0; t < 500; t += 7.0f) ASSERT(wind_gust(&w, 0, t) >= 0.0f);
    PASS();
}

TEST weight_peaks_at_center(void) {
    WindField w = make_field();
    ASSERT_IN_RANGE(1.0f, wind_weight(&w, 0, w.center[0]), 1e-4f);
    ASSERT(wind_weight(&w, 0, w.center[0] + 20.0f) < 1.0f);
    /* zero beyond 1.5 * ech_height */
    ASSERT_IN_RANGE(0.0f, wind_weight(&w, 0, w.center[0] + 1.5f*w.ech_height + 1.0f), 1e-4f);
    PASS();
}

TEST target_max_on_cloud_center(void) {
    WindField w = make_field();
    /* far apart so neighbor weight ~0: put echelons 200px apart */
    w.center[1] = 320.0f;
    /* with zero gust freqs, gust = 0.5+0.5*sin(0)+0.3*sin(0) = 0.5 */
    float on = wind_target(&w, w.center[0], 0.0f);
    ASSERT(fabsf(on - 0.5f * w.base[0]) < 0.2f); /* ~ base*gust at center */
    ASSERT(on > 0.0f);
    PASS();
}

TEST opposite_neighbor_reverses(void) {
    WindField w = make_field();          /* top +, bottom -, 80px apart, overlap */
    float at_top    = wind_target(&w, w.center[0], 0.0f);  /* near + cloud */
    float at_bottom = wind_target(&w, w.center[1], 0.0f);  /* near - cloud */
    ASSERT(at_top > 0.0f);
    ASSERT(at_bottom < 0.0f);            /* crossing over reverses sign */
    float midhi = wind_target(&w, w.center[0] + 0.5f*(w.center[1]-w.center[0]), 0.0f);
    ASSERT(fabsf(midhi) < fabsf(at_top)); /* between them: slowed */
    PASS();
}

TEST step_eases_and_stalls(void) {
    WindField w = make_field();
    w.center[1] = 320.0f;
    float target = wind_target(&w, w.center[0], 0.0f);
    float vx = 0.0f;
    for (int i = 0; i < 200; i++) { vx = wind_step_vx(&w, vx, w.center[0], 0.0f); }
    ASSERT(fabsf(vx - target) < 0.05f);        /* converges toward target */
    /* stall: empty region (y far from any cloud) with small vx -> 0 */
    float empty_y = 600.0f; /* beyond influence of both */
    float sv = wind_step_vx(&w, 0.02f, empty_y, 0.0f);
    ASSERT_IN_RANGE(0.0f, sv, 1e-4f);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char** argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(gust_nonnegative);
    RUN_TEST(weight_peaks_at_center);
    RUN_TEST(target_max_on_cloud_center);
    RUN_TEST(opposite_neighbor_reverses);
    RUN_TEST(step_eases_and_stalls);
    GREATEST_MAIN_END();
}
