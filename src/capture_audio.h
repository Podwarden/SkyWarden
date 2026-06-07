#ifndef BALLY_CAPTURE_AUDIO_H
#define BALLY_CAPTURE_AUDIO_H

/* Offline (host-side) audio renderer for the BALLY_SHOT capture harness.
 *
 * Produces a sample-accurate WAV that matches the scripted playthrough frame for
 * frame: the adaptive chiptune music (rendered via music_render_offline) mixed
 * with re-synthesized SFX (burner whoosh / gun / explosion) using the same
 * parameters as src/audio.c. One frame == 44100/30 == 1470 mono samples.
 *
 * The whole module is sim-only; outside BALLY_SHOT it compiles to nothing. */

#if defined(BALLY_SHOT) && defined(TARGET_SIMULATOR)

void capture_audio_begin(const char* wav_path);
void capture_audio_frame(void);   /* render + append exactly one video frame's audio */
void capture_audio_end(void);     /* patch WAV sizes and close */

/* SFX events, forwarded from src/audio.c during the game body. The burner level
 * is the per-frame gain (0..BURNER_MAX); gun/explosion are one-shot triggers. */
void capture_sfx_burner(float level);
void capture_sfx_gun(void);
void capture_sfx_explosion(void);

#endif
#endif
