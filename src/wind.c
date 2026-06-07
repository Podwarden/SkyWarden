#include "wind.h"
#include <math.h>

float wind_gust(const WindField* w, int i, float t) {
    float g = 0.5f + 0.5f * sinf(t * w->f1[i] + w->p1[i])
                   + 0.3f * sinf(t * w->f2[i] + w->p2[i]);
    return g < 0.0f ? 0.0f : g;
}

float wind_eff(const WindField* w, int i, float t) {
    return w->base[i] * wind_gust(w, i, t);
}

float wind_weight(const WindField* w, int i, float y) {
    float d = fabsf(y - w->center[i]);
    float r = 1.5f * w->ech_height;
    float wt = 1.0f - d / r;
    return wt < 0.0f ? 0.0f : wt;
}

float wind_target(const WindField* w, float y, float t) {
    float target = 0.0f;
    for (int i = 0; i < w->count; i++) {
        if (w->base[i] != 0.0f)
            target += wind_weight(w, i, y) * wind_eff(w, i, t);
    }
    return target;
}

float wind_step_vx(const WindField* w, float vx, float y, float t) {
    float target = wind_target(w, y, t);
    float nv = vx + (target - vx) * w->wind_ease;
    if (fabsf(target) < 0.03f && fabsf(nv) < 0.05f) nv = 0.0f; /* stall */
    return nv;
}
