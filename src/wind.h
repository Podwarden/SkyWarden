#ifndef BALLY_WIND_H
#define BALLY_WIND_H

#define WIND_MAX_ECHELONS 12

/* Altitude-layered gusty wind field. Pure; no Playdate deps.
 * Horizontal speed is a proximity-weighted blend of nearby echelons' winds. */
typedef struct {
    int   count;
    float center[WIND_MAX_ECHELONS]; /* y of each echelon center (px) */
    float base[WIND_MAX_ECHELONS];   /* signed base wind; 0.0 = no cloud */
    float f1[WIND_MAX_ECHELONS], p1[WIND_MAX_ECHELONS];
    float f2[WIND_MAX_ECHELONS], p2[WIND_MAX_ECHELONS]; /* gust freqs/phases */
    float ech_height;                /* echelon height (=0.7 * balloon height) */
    float wind_ease;                 /* vx easing factor toward target */
} WindField;

float wind_gust(const WindField* w, int i, float t);   /* >=0 gust multiplier */
float wind_eff(const WindField* w, int i, float t);    /* base * gust */
float wind_weight(const WindField* w, int i, float y); /* triangular falloff 0..1 */
float wind_target(const WindField* w, float y, float t);            /* blended target vx */
float wind_step_vx(const WindField* w, float vx, float y, float t); /* eased vx + stall */

#endif
