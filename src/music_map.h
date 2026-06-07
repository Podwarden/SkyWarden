#ifndef SKYWARDEN_MUSIC_MAP_H
#define SKYWARDEN_MUSIC_MAP_H

/* Adaptive-music mapping (pure; no Playdate/engine deps). Tunable constants. */
#define MUSIC_FLOOR_RATIO   0.85f   /* calm in-game tempo */
#define MUSIC_CEIL_RATIO    1.6f    /* full-action tempo */
#define MUSIC_PAD_RATIO     0.55f   /* on the launchpad: below the in-game floor */
#define MUSIC_PARTS         3       /* number of intensity bands -> song parts */

/* intensity 0..1 -> tempo multiplier. on_pad overrides to MUSIC_PAD_RATIO.
 * In-game: lerp FLOOR..CEIL by clamped intensity (never below FLOOR). */
float music_tempo_ratio(float intensity, int on_pad);

/* intensity 0..1 -> song part band index 0..MUSIC_PARTS-1 (calm..busy). */
int music_part_index(float intensity);

#endif
