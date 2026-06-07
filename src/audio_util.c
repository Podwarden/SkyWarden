#include "audio_util.h"
#include <math.h>

float audio_burner_level(float crank_speed) {
    float s = fabsf(crank_speed) / BURNER_CRANK_FULL;   /* 0..1 over the ramp */
    if (s > 1.0f) s = 1.0f;
    return s * BURNER_MAX;
}
