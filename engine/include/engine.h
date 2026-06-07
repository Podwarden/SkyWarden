#ifndef ENGINE_H
#define ENGINE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    ENG_FMT_UNKNOWN = 0,
    ENG_FMT_PSG     = 1,
    ENG_FMT_VTX     = 2,
    ENG_FMT_YM      = 3,
    ENG_FMT_PT3     = 4,
    /* 5 = STC (reserved), 6 = SQT (reserved) */
    ENG_FMT_AY      = 7,
} EngineFormat;

EngineFormat engine_detect_format(const uint8_t* buf, size_t len, const char* path);

typedef struct Engine Engine;

Engine* engine_new(void);
void    engine_free(Engine* e);

/* Loads a tune from an in-memory buffer. Buffer must remain valid until unload.
 * Returns 1 on success, 0 on unknown format / parse failure. */
int  engine_load(Engine* e, const uint8_t* buf, size_t len, const char* path,
                 int is_ym, int sample_rate);

void engine_unload(Engine* e);

/* Render `n_samples` mono int16 samples into `out`. Returns number actually
 * written (may be < n_samples if tune ended; for now PSG loops, so usually n_samples).
 * Returns 0 if no tune is loaded, or if n_samples <= 0. */
int  engine_render(Engine* e, int16_t* out, int n_samples);

/* Effective frame rate (Hz) the sequencer is currently advancing at, derived
 * from the current samples-per-frame. With the default speed of 1.0 this is the
 * tune's native frame rate; engine_set_speed scales it. Returns 0 if no tune is
 * loaded. */
int  engine_frame_rate(const Engine* e);

/* Pitch-preserving tempo multiplier: >1 faster, <1 slower. Scales how fast the
 * sequencer advances; audio pitch is unaffected. Clamped to [0.25, 3.0].
 * Default 1.0 (no behavior change). */
void engine_set_speed(Engine* e, float ratio);

/* Live mute of AY channels. mask bit0=A, bit1=B, bit2=C. Muted channels are
 * silenced (volume 0, tone+noise off) each frame without altering the reported
 * register stream. Default 0 (no mute). */
void engine_set_channel_mute(Engine* e, int mask);

/* Seek the (PT3) sequencer to order-list position `pos`. No-op for formats that
 * don't support seeking. Clamped to the tune's position range. */
void engine_set_position(Engine* e, int pos);

/* Read the (PT3) sequencer's current order-list position. Returns -1 for
 * formats that don't support it / when no tune is loaded. Used to detect
 * pattern boundaries for boundary-aligned part switching. */
int engine_get_position(const Engine* e);

/* ------------------------------------------------------------------------
 * Diagnostic telemetry (observation only; no behavior change).
 *
 * Used to debug .ay audio breakup. The two values we care about most:
 *   - env_retrigs:   how often apply_regs_to_chip retriggers the AY envelope
 *                    (called every audio sample under the per-sample stepping
 *                    scheme; we want to know if 0xFF sentinel is being
 *                    cleared and the chip is being spammed).
 *   - z80.out_writes / r13_writes: actual register write counts from the
 *                    emulated player. If r13_writes is small but env_retrigs
 *                    is huge → confirms "register sticks at 0x09" hypothesis.
 * ------------------------------------------------------------------------ */
typedef struct EngineDiag {
    uint32_t apply_calls;
    uint32_t env_retrigs;
    uint32_t r13_nonsentinel;
    uint8_t  last_r13;
    uint8_t  last_r7;
    uint16_t last_tone_a;
} EngineDiag;

void engine_diag_read(EngineDiag* out);
void engine_diag_reset(void);

/* Forward-declared here to avoid pulling format internals into the public
 * engine header. Callers (e.g. pdex.c) must #include
 * "formats/ay_z80_glue.h" to see the actual layout. */
struct AyZ80Diag;

/* Reads/resets the currently-loaded format's Z80 diag iff the format is .ay.
 * Returns 1 on success, 0 if the engine has no .ay tune loaded. */
int engine_ay_z80_diag_read(const Engine* e, struct AyZ80Diag* out);
int engine_ay_z80_diag_reset(Engine* e);

/* Multi-song .ay support. Returns 0 if no .ay tune is loaded, else 1..N. */
int engine_ay_song_count(const Engine* e);
int engine_ay_current_song(const Engine* e);
/* Restart playback at song_index. Returns 0 if no .ay loaded or out of
 * range; 1 on success. The engine sample-position state is reset. */
int engine_ay_set_song(Engine* e, int song_index);

#endif
