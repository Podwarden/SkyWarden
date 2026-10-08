#ifndef BALLY_DECOR_H
#define BALLY_DECOR_H
#define STAR_COUNT 14
/* fixed screen-space star positions + per-star twinkle speed (pure data + phase) */
typedef struct { short x, y; float sp, ph0; } Star;
void  decor_init_stars(Star* stars);                 /* deterministic layout */
int   decor_twinkle_phase(const Star* s, float t);   /* 0..3 current twinkle frame */
#endif
