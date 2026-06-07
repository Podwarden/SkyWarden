#ifndef BALLY_AUDIO_UTIL_H
#define BALLY_AUDIO_UTIL_H

/* Pure audio helpers (no Playdate deps), so the tunable burner-loudness curve
 * has a host-testable seam. The burner whoosh is deliberately quiet: a low
 * ceiling keeps a continuous sound from masking music / other SFX. */

/* Loudness ceiling for the burner whoosh (0..1 linear gain). Low on purpose. */
#define BURNER_MAX        0.18f
/* Crank speed (abs degrees/frame) at/above which the whoosh reaches BURNER_MAX. */
#define BURNER_CRANK_FULL 30.0f

/* Map crank speed -> burner gain. 0 at rest; rises with |crank_speed|;
 * clamped to [0, BURNER_MAX]. Negative speeds (cranking backwards) count too. */
float audio_burner_level(float crank_speed);

#endif
