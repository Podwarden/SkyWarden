#include "music_map.h"

float music_tempo_ratio(float intensity, int on_pad) {
    if (on_pad) return MUSIC_PAD_RATIO;
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    return MUSIC_FLOOR_RATIO + (MUSIC_CEIL_RATIO - MUSIC_FLOOR_RATIO) * intensity;
}

int music_part_index(float intensity) {
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    int p = (int)(intensity * MUSIC_PARTS);
    if (p >= MUSIC_PARTS) p = MUSIC_PARTS - 1;
    return p;
}
