#include "capture_audio.h"

/* Non-empty translation unit even when the harness is not built. */
typedef int capture_audio_translation_unit;

#if defined(BALLY_SHOT) && defined(TARGET_SIMULATOR)

#include "music.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define SR        44100
#define FPS       30
#define FRAME     (SR / FPS)        /* 1470 mono samples per video frame */

/* SFX shaping — mirrors src/audio.c levels; envelopes are exponential noise
 * bursts (percussive), tuned to the same loudness ceilings. */
#define BURNER_CUTOFF 900.0f
#define GUN_VOL       0.15f
#define EXPL_VOL      0.25f
#define GUN_TAU       0.020f        /* s, exp decay */
#define EXPL_TAU      0.130f        /* s, exp decay */
#define MAX_VOICES    8

static FILE*   g_wav   = NULL;
static uint32_t g_nsamples = 0;     /* total mono samples written */

/* deterministic white noise */
static uint32_t g_rng = 0x1234567u;
static inline float noise(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)((int)(g_rng >> 9) & 0x7fffff) / (float)0x400000 * 2.0f - 1.0f;
}

/* burner: low-pass-filtered noise scaled by an eased per-frame level */
static float g_burner_target = 0.0f, g_burner_cur = 0.0f, g_lp = 0.0f;
static const float LP_A = 0.1203f;  /* 1 - exp(-2*pi*900/44100) */

/* one-shot gun/explosion voices */
typedef struct { int active; float t; float tau, vol; } Voice;
static Voice g_voices[MAX_VOICES];

static void voice_start(float tau, float vol) {
    for (int i = 0; i < MAX_VOICES; i++) if (!g_voices[i].active) {
        g_voices[i].active = 1; g_voices[i].t = 0.0f;
        g_voices[i].tau = tau; g_voices[i].vol = vol; return;
    }
}

static void wr16(int16_t v) { fputc(v & 0xff, g_wav); fputc((v >> 8) & 0xff, g_wav); }
static void wr32(uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (8*i)) & 0xff, g_wav); }

void capture_audio_begin(const char* wav_path) {
    g_wav = fopen(wav_path, "wb");
    if (!g_wav) return;
    /* WAV header, mono 16-bit @ SR; sizes patched in capture_audio_end */
    fwrite("RIFF", 1, 4, g_wav); wr32(0);            /* RIFF size (patch) */
    fwrite("WAVE", 1, 4, g_wav);
    fwrite("fmt ", 1, 4, g_wav); wr32(16);
    wr16(1); wr16(1);                                 /* PCM, mono */
    wr32(SR); wr32(SR * 2);                           /* byte rate */
    wr16(2); wr16(16);                                /* block align, bits */
    fwrite("data", 1, 4, g_wav); wr32(0);            /* data size (patch) */
    g_nsamples = 0;
}

void capture_sfx_burner(float level)   { g_burner_target = level; }
void capture_sfx_gun(void)             { voice_start(GUN_TAU,  GUN_VOL); }
void capture_sfx_explosion(void)       { voice_start(EXPL_TAU, EXPL_VOL); }

void capture_audio_frame(void) {
    if (!g_wav) return;
    int16_t music[FRAME];
    music_render_offline(music, FRAME);   /* adaptive chiptune, already scene-scaled */

    for (int i = 0; i < FRAME; i++) {
        /* burner: eased LP-noise whoosh */
        g_burner_cur += (g_burner_target - g_burner_cur) * 0.02f;
        g_lp += LP_A * (noise() - g_lp);
        float sfx = g_lp * g_burner_cur;

        /* one-shot voices */
        for (int v = 0; v < MAX_VOICES; v++) if (g_voices[v].active) {
            float a = expf(-g_voices[v].t / g_voices[v].tau);
            if (a < 0.01f) { g_voices[v].active = 0; continue; }
            sfx += noise() * a * g_voices[v].vol;
            g_voices[v].t += 1.0f / SR;
        }

        int s = music[i] + (int)(sfx * 32767.0f);
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        wr16((int16_t)s);
    }
    g_nsamples += FRAME;
}

void capture_audio_end(void) {
    if (!g_wav) return;
    uint32_t data_bytes = g_nsamples * 2;
    fseek(g_wav, 4, SEEK_SET);  wr32(36 + data_bytes);   /* RIFF size */
    fseek(g_wav, 40, SEEK_SET); wr32(data_bytes);        /* data size */
    fclose(g_wav); g_wav = NULL;
}

#endif
