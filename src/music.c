#include "music.h"
#include "music_map.h"
#include "engine.h"
#include <stdlib.h>
#include <string.h>

/* Scene-driven music with a SINGLE engine that swaps which tune it plays.
 *
 * Why one engine: the vendored PT3 player (third_party/pt3/pt3.c) keeps its
 * state in FILE-SCOPE GLOBALS (PlParams[]/PlConsts[]), so it is not reentrant —
 * two simultaneous PT3 engines collide and both render the last-loaded tune.
 * So we keep ONE engine and reload the desired tune per scene, swapping only
 * when the gain has faded to ~0 (the intro→game transition passes through the
 * silent launch build-up, so the swap is inaudible; menu↔game-over dips briefly).
 *
 * Scenes:  INTRO (Splash/Menu/About) -> Kidsoft 204; SILENT (Tutorial + launch
 * build-up) -> fade out; GAME (pad retracts onward) -> adaptive panimoja.
 *
 * Per-track gain is applied INSIDE the callback — a raw addSource callback source
 * ignores source->setVolume. */

#define INTRO_VOL    0.6f
#define GAME_VOL     0.62f       /* music sits under the SFX */
#define VOL_EASE     0.06f       /* per-frame fade ease */
#define PART_MARGIN  0.08f       /* hysteresis dead-zone before committing a new part */

enum { TUNE_NONE, TUNE_INTRO, TUNE_GAME };

static PlaydateAPI* pd = NULL;
static Engine*      g_eng = NULL;
static SoundSource* g_src = NULL;
static uint8_t*     g_intro_buf = NULL; static int g_intro_len = 0;
static uint8_t*     g_game_buf  = NULL; static int g_game_len  = 0;
static int   g_cur_tune  = TUNE_NONE;    /* currently loaded in the engine */
static int   g_want_tune = TUNE_INTRO;   /* desired by the current scene */
static float g_vol       = 0.0f;         /* eased output gain */
static int   g_paused    = 0;

/* adaptive state — applies only while the GAME tune is loaded */
static float g_ratio       = 1.0f;
static float g_intensity   = 0.0f;
static int   g_desired_part = 0;
static int   g_active_part  = -1;
static int   g_last_pos     = -1;
/* calm..busy order positions; none in the intro region (0..3) so a re-seek never
 * sounds like a restart. */
static const int PART_POS[MUSIC_PARTS] = { 6, 11, 19 };

static int music_cb(void* ctx, int16_t* left, int16_t* right, int len) {
    (void)ctx;
    float v = g_paused ? 0.0f : g_vol;
    if (!g_eng || v <= 0.0f) {
        memset(left,  0, (size_t)len * sizeof(int16_t));
        memset(right, 0, (size_t)len * sizeof(int16_t));
        return 1;
    }
    engine_render(g_eng, left, len);                 /* mono */
    if (v < 1.0f) for (int i = 0; i < len; i++) left[i] = (int16_t)((int)left[i] * v);
    memcpy(right, left, (size_t)len * sizeof(int16_t));
    return 1;
}

static uint8_t* slurp(const char* path, int* outlen) {
    *outlen = 0;
    SDFile* f = pd->file->open(path, kFileRead);
    if (!f) { pd->system->logToConsole("music: %s open failed", path); return NULL; }
    pd->file->seek(f, 0, SEEK_END); int n = pd->file->tell(f); pd->file->seek(f, 0, SEEK_SET);
    if (n <= 0) { pd->file->close(f); return NULL; }
    uint8_t* b = malloc((size_t)n);
    if (!b) { pd->file->close(f); return NULL; }
    pd->file->read(f, b, (unsigned)n); pd->file->close(f);
    *outlen = n;
    return b;
}

/* Reload the engine with the requested tune (the engine reuses one player slot). */
static void load_tune(int tune) {
    if (!g_eng) return;
    if (tune == TUNE_INTRO && g_intro_buf) {
        engine_load(g_eng, g_intro_buf, (size_t)g_intro_len, "kidsoft204.pt3", 0, 44100);
    } else if (tune == TUNE_GAME && g_game_buf) {
        engine_load(g_eng, g_game_buf, (size_t)g_game_len, "panimoja.pt3", 0, 44100);
        engine_set_speed(g_eng, MUSIC_FLOOR_RATIO);
        g_ratio = MUSIC_FLOOR_RATIO;
        g_active_part = -1; g_last_pos = -1;       /* fresh part tracking */
    } else {
        return;
    }
    g_cur_tune = tune;
}

void music_init(PlaydateAPI* p) {
    pd = p;
    g_intro_buf = slurp("tunes/kidsoft204.pt3", &g_intro_len);
    g_game_buf  = slurp("tunes/panimoja.pt3",   &g_game_len);
    g_eng = engine_new();
    g_intensity = 0.0f; g_desired_part = 0; g_active_part = -1; g_last_pos = -1;
    g_vol = 0.0f; g_paused = 0; g_want_tune = TUNE_INTRO;
    load_tune(TUNE_INTRO);                          /* start on the intro track */
#if defined(BALLY_SHOT) && defined(TARGET_SIMULATOR)
    /* Capture build: do NOT register the audio callback. The engine must be
     * advanced by exactly one music_render_offline() per video frame, so a live
     * callback rendering on the audio thread would double-advance and desync. */
    (void)music_cb; g_src = NULL;
#else
    g_src = pd->sound->addSource(music_cb, NULL, /*stereo*/1);
#endif
}

void music_tick(int scene, float intensity, int on_pad, int missiles) {
    if (!g_eng) return;

    /* desired tune + target gain for this scene */
    int want; float tvol;
    if      (scene == MUSIC_INTRO) { want = TUNE_INTRO;  tvol = INTRO_VOL; }
    else if (scene == MUSIC_GAME)  { want = TUNE_GAME;   tvol = GAME_VOL;  }
    else /* MUSIC_SILENT */        { want = g_want_tune; tvol = 0.0f;      }
    g_want_tune = want;

    /* If a different tune is wanted than is loaded, fade to silence FIRST, then
     * swap (inaudible). Otherwise ease toward the scene volume. */
    float target = (g_cur_tune == want) ? tvol : 0.0f;
    g_vol += (target - g_vol) * VOL_EASE;
    if (g_cur_tune != want && g_vol < 0.02f) load_tune(want);

    /* Adaptive tempo/mute/part applies ONLY while the game tune plays. */
    if (g_cur_tune != TUNE_GAME) { engine_set_channel_mute(g_eng, 0x0); return; }

    g_intensity += (intensity - g_intensity) * 0.02f;          /* heavy smoothing */
    float tr = music_tempo_ratio(g_intensity, on_pad);
    g_ratio += (tr - g_ratio) * 0.03f;                          /* gentle tempo ease */
    engine_set_speed(g_eng, g_ratio);
    engine_set_channel_mute(g_eng, missiles ? 0x3 : 0x0);       /* mute channels A+B while a missile is visible; unmute when clear */

    int wantp = music_part_index(g_intensity);                 /* hysteresis-gated part */
    if (wantp != g_desired_part) {
        int below = music_part_index(g_intensity - PART_MARGIN);
        int above = music_part_index(g_intensity + PART_MARGIN);
        if ((wantp > g_desired_part && below == wantp) ||
            (wantp < g_desired_part && above == wantp)) g_desired_part = wantp;
    }
    int pos = engine_get_position(g_eng);                       /* switch only at boundaries */
    if (pos != g_last_pos) {
        if (pos >= 0 && g_desired_part != g_active_part) {
            engine_set_position(g_eng, PART_POS[g_desired_part]);
            g_active_part = g_desired_part;
        }
        g_last_pos = engine_get_position(g_eng);
    }
}

void music_set_paused(int paused) { g_paused = paused; }

#if defined(BALLY_SHOT) && defined(TARGET_SIMULATOR)
int music_render_offline(int16_t* out, int n_samples) {
    float v = g_paused ? 0.0f : g_vol;
    if (!g_eng || v <= 0.0f) { memset(out, 0, (size_t)n_samples * sizeof(int16_t)); return 1; }
    engine_render(g_eng, out, n_samples);   /* mono; same path as music_cb */
    if (v < 1.0f) for (int i = 0; i < n_samples; i++) out[i] = (int16_t)((int)out[i] * v);
    return 1;
}
#endif
