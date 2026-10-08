#ifndef BALLY_AUDIO_H
#define BALLY_AUDIO_H

#include "pd_api.h"

/* Runtime-synthesized SFX. All effects are quiet by design (see audio_util.h /
 * audio.c constants) to leave headroom for future music. Call audio_init once
 * at startup; the others from the game loop. */
void audio_init(PlaydateAPI* pd);

/* Set the looped burner whoosh for this frame. on=0 silences it; otherwise the
 * level follows crank_speed via audio_burner_level(). Safe to call every frame. */
void audio_burner(int on, float crank_speed);

/* One-shot: an AA gun fired a missile (subtle "spit"). */
void audio_gun_fire(void);

/* One-shot: a missile hit the balloon (restrained "boom"). */
void audio_explosion(void);

#endif
