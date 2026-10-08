#include "engine.h"
#include "z80emu.h"   /* Z80_STATE / Z80_A — register-A seed for engine_ay_set_song */
#include "formats/psg.h"
#include "formats/vtx.h"
#include "formats/ym.h"
#include "formats/pt3.h"
#include "formats/ay.h"
#include "formats/format_vtable.h"
#include "ay_mini.h"
#include <stdlib.h>
#include <string.h>

/* Maximum per-format state we need to embed. PsgFile is the largest current state.
 * Bump as needed when new formats arrive; the _Static_assert below catches overflows. */
#define ENGINE_MAX_STATE 256

struct Engine {
    EngineFormat        fmt;
    int                 sample_rate;
    int                 samples_per_frame;
    int                 base_spf;        /* unscaled samples/frame for speed control */
    float               speed;           /* tempo multiplier (engine_set_speed) */
    int                 mute_mask;       /* live AY channel mute: bit0=A, bit1=B, bit2=C */
    int                 samples_until_next_frame;
    int                 is_ym;          /* AY-3-8910 vs YM2149 mixing — passed to ay_mini_init */
    AyMini              chip;
    const FormatVTable* vt;        /* NULL when no tune loaded */
    const uint8_t*      buf;       /* original tune buffer, owned by caller */
    size_t              buflen;
    /* Per-format opaque state. Wrapped in a union to force 8-byte alignment
     * so format-state structs containing pointers / size_t / double work on
     * 32-bit ARM (Cortex-M7) where 8-byte fields require 8-byte alignment. */
    union {
        uint8_t b[ENGINE_MAX_STATE];
        double  _align;
    } state;
    uint8_t             regs[14];   /* current AY register state */
};

_Static_assert(sizeof(PsgFile) <= ENGINE_MAX_STATE,
               "ENGINE_MAX_STATE too small for PsgFile");
_Static_assert(sizeof(VtxFile) <= ENGINE_MAX_STATE,
               "ENGINE_MAX_STATE too small for VtxFile");
_Static_assert(sizeof(YmFile) <= ENGINE_MAX_STATE,
               "ENGINE_MAX_STATE too small for YmFile");
_Static_assert(sizeof(Pt3File) <= ENGINE_MAX_STATE,
               "ENGINE_MAX_STATE too small for Pt3File");
_Static_assert(sizeof(AyFile) <= ENGINE_MAX_STATE,
               "ENGINE_MAX_STATE too small for AyFile");
_Static_assert(_Alignof(union { uint8_t b[ENGINE_MAX_STATE]; double _align; }) >= 8,
               "Engine.state must be 8-byte aligned for future format structs");

/* Diagnostic counters — see playdate/src/pdex.c for the read-side. Cleared
 * via engine_diag_reset. No effect on audio output. Single audio thread writes,
 * Lua thread reads; volatile + naturally-aligned uint32_t is sufficient on
 * Cortex-M7 (no tearing). */
volatile uint32_t g_engine_diag_apply_calls     = 0;
volatile uint32_t g_engine_diag_env_retrigs     = 0;
volatile uint32_t g_engine_diag_r13_nonsentinel = 0;
volatile uint8_t  g_engine_diag_last_r13        = 0xFF;
volatile uint8_t  g_engine_diag_last_r7         = 0xFF;
volatile uint16_t g_engine_diag_last_tone_a     = 0;

Engine* engine_new(void) {
    Engine* e = calloc(1, sizeof(Engine));
    return e;
}

void engine_free(Engine* e) { free(e); }

static void apply_regs_to_chip(Engine* e) {
    g_engine_diag_apply_calls++;
    g_engine_diag_last_r7 = e->regs[7];
    g_engine_diag_last_tone_a = (uint16_t)(((e->regs[1] & 0x0F) << 8) | e->regs[0]);
    if (e->regs[13] != 0xFF) g_engine_diag_r13_nonsentinel++;

    /* Translate raw AY register bytes into ay_mini setter calls. */
    /* R0/R1: channel A tone period (12-bit) */
    ay_mini_set_tone(&e->chip, 0, ((e->regs[1] & 0x0F) << 8) | e->regs[0]);
    ay_mini_set_tone(&e->chip, 1, ((e->regs[3] & 0x0F) << 8) | e->regs[2]);
    ay_mini_set_tone(&e->chip, 2, ((e->regs[5] & 0x0F) << 8) | e->regs[4]);
    /* R6: noise period (5-bit) */
    ay_mini_set_noise(&e->chip, e->regs[6] & 0x1F);
    /* R7: mixer */
    for (int ch = 0; ch < 3; ++ch) {
        if (e->mute_mask & (1 << ch)) {
            /* Muted: force this channel silent (tone off, noise off, env off,
             * volume 0) without touching e->regs so engine_current_regs still
             * reports the true register stream. */
            ay_mini_set_mixer(&e->chip, ch, 1, 1, 0);
            ay_mini_set_volume(&e->chip, ch, 0);
        } else {
            int t_off = (e->regs[7] >> ch)        & 1;
            int n_off = (e->regs[7] >> (ch + 3))  & 1;
            int e_on  = (e->regs[8 + ch] >> 4)    & 1;
            ay_mini_set_mixer(&e->chip, ch, t_off, n_off, e_on);
            ay_mini_set_volume(&e->chip, ch, e->regs[8 + ch] & 0x0F);
        }
    }
    /* RB/RC: envelope period (16-bit), RD: envelope shape (writing it resets envelope) */
    ay_mini_set_envelope(&e->chip, (e->regs[12] << 8) | e->regs[11]);
    /* Only re-arm envelope when shape byte was written; sentinel 0xFF means
     * "not written this frame". */
    if (e->regs[13] != 0xFF) {
        ay_mini_set_envelope_shape(&e->chip, e->regs[13]);
        g_engine_diag_env_retrigs++;
        g_engine_diag_last_r13 = e->regs[13];
    }
}

static void advance_one_frame(Engine* e) {
    if (!e->vt) return;
    if (!e->vt->next_frame(e->state.b, e->regs)) {
        /* End of stream. Re-open the buffer so the format's cursor resets.
         * If re-open fails (e.g. LH5 decompression OOM, corrupted buffer),
         * skip next_frame — registers stay at their last values (frozen
         * audio) instead of dereferencing uninitialised state. */
        int fr;  /* frame_rate is already locked into samples_per_frame */
        if (e->vt->open(e->state.b, e->buf, e->buflen, &fr)) {
            e->vt->next_frame(e->state.b, e->regs);
        }
    }
    apply_regs_to_chip(e);
}

int engine_load(Engine* e, const uint8_t* buf, size_t len, const char* path,
                int is_ym, int sample_rate) {
    engine_unload(e);

    EngineFormat f = engine_detect_format(buf, len, path);
    const FormatVTable* vt = NULL;
    switch (f) {
        case ENG_FMT_PSG: vt = &psg_vtable; break;
        case ENG_FMT_VTX: vt = &vtx_vtable; break;
        case ENG_FMT_YM:  vt = &ym_vtable;  break;
        case ENG_FMT_PT3: vt = &pt3_vtable; break;
        case ENG_FMT_AY:  vt = &ay_vtable;  break;
        default:          return 0;
    }
    if (vt->state_size > ENGINE_MAX_STATE) return 0;

    int frame_rate = 50;
    if (!vt->open(e->state.b, buf, len, &frame_rate)) return 0;

    e->fmt                      = f;
    e->vt                       = vt;
    e->buf                      = buf;
    e->buflen                   = len;
    e->sample_rate              = sample_rate;
    e->samples_per_frame        = sample_rate / frame_rate;
    e->base_spf                 = e->samples_per_frame;   /* unscaled samples/frame for speed control */
    e->speed                    = 1.0f;
    e->mute_mask                = 0;    /* fresh tune starts unmuted */
    e->samples_until_next_frame = 0;   /* render first frame immediately */
    e->is_ym                    = is_ym;
    memset(e->regs, 0, sizeof(e->regs));
    e->regs[7]  = 0x3F;   /* mixer: everything off by default */
    e->regs[13] = 0xFF;   /* envelope shape sentinel: not written */
    ay_mini_init(&e->chip, is_ym, sample_rate);
    return 1;
}

void engine_unload(Engine* e) {
    if (e->vt) { e->vt->close(e->state.b); }
    e->vt     = NULL;
    e->fmt    = ENG_FMT_UNKNOWN;
    e->buf    = NULL;
    e->buflen = 0;
}

int engine_render(Engine* e, int16_t* out, int n_samples) {
    if (e->fmt == ENG_FMT_UNKNOWN || n_samples <= 0) return 0;
    for (int i = 0; i < n_samples; ++i) {
        if (e->samples_until_next_frame <= 0) {
            advance_one_frame(e);
            e->samples_until_next_frame = e->samples_per_frame;
        }
        e->samples_until_next_frame--;
        out[i] = ay_mini_step(&e->chip);
    }
    return n_samples;
}

int engine_frame_rate(const Engine* e) {
    if (!e || e->fmt == ENG_FMT_UNKNOWN || e->samples_per_frame <= 0) return 0;
    return e->sample_rate / e->samples_per_frame;
}

void engine_set_speed(Engine* e, float ratio) {
    if (!e || e->base_spf <= 0) return;
    if (ratio < 0.25f) ratio = 0.25f;
    if (ratio > 3.0f)  ratio = 3.0f;
    e->speed = ratio;
    int spf = (int)((float)e->base_spf / ratio + 0.5f);
    if (spf < 1) spf = 1;
    e->samples_per_frame = spf;   /* next frame boundary uses the scaled length */
}

void engine_set_channel_mute(Engine* e, int mask) { if (e) e->mute_mask = mask & 0x7; }

void engine_set_position(Engine* e, int pos) {
    if (e && e->vt && e->vt->set_position) e->vt->set_position(e->state.b, pos);
}

int engine_get_position(const Engine* e) {
    return (e && e->vt && e->vt->get_position) ? e->vt->get_position(e->state.b) : -1;
}

/* ------------------------------------------------------------------------
 * Diagnostic API (observation only).
 * ------------------------------------------------------------------------ */

void engine_diag_read(EngineDiag* out) {
    out->apply_calls     = g_engine_diag_apply_calls;
    out->env_retrigs     = g_engine_diag_env_retrigs;
    out->r13_nonsentinel = g_engine_diag_r13_nonsentinel;
    out->last_r13        = g_engine_diag_last_r13;
    out->last_r7         = g_engine_diag_last_r7;
    out->last_tone_a     = g_engine_diag_last_tone_a;
}

void engine_diag_reset(void) {
    g_engine_diag_apply_calls = 0;
    g_engine_diag_env_retrigs = 0;
    g_engine_diag_r13_nonsentinel = 0;
}

/* Read .ay-format Z80 diag iff the currently-loaded format is AY. Returns 1 on
 * success, 0 if no .ay tune is active. */
int engine_ay_z80_diag_read(const Engine* e, AyZ80Diag* out) {
    if (!e || !e->vt || e->fmt != ENG_FMT_AY) return 0;
    const AyFile* a = (const AyFile*)e->state.b;
    ay_z80_get_diag(&a->z80, out);
    return 1;
}

int engine_ay_z80_diag_reset(Engine* e) {
    if (!e || !e->vt || e->fmt != ENG_FMT_AY) return 0;
    AyFile* a = (AyFile*)e->state.b;
    ay_z80_reset_diag(&a->z80);
    return 1;
}

int engine_ay_song_count(const Engine* e) {
    if (!e || !e->vt || e->fmt != ENG_FMT_AY) return 0;
    const AyFile* a = (const AyFile*)e->state.b;
    return ay_song_count(a);
}

int engine_ay_current_song(const Engine* e) {
    if (!e || !e->vt || e->fmt != ENG_FMT_AY) return 0;
    const AyFile* a = (const AyFile*)e->state.b;
    return ay_current_song(a);
}

int engine_ay_set_song(Engine* e, int song_index) {
    if (!e || !e->vt || e->fmt != ENG_FMT_AY) return 0;
    AyFile* a = (AyFile*)e->state.b;
    if (!a->loaded) return 0;
    if (song_index < 0 || song_index >= (int)a->num_songs) return 0;
    if (!a->tune_buf || !a->tune_len) return 0;

    /* State-preserving subsong switch.
     *
     * Per Project AY convention, song 0's INIT establishes the shared
     * player state (variable tables, channel pointers, etc) in Z80 RAM.
     * Songs 1..N typically have INIT=0 and differ only in their INT
     * vector: the running player checks "which subsong am I" via the
     * address it was dispatched to, and that address is whatever the
     * RST 0x38 stub at 0x0038 currently points to (or, for IM 2 tunes,
     * whatever the vector table says — those don't use RST 0x38).
     *
     * So to switch subsongs without breaking the player we MUST NOT
     * tear down the Z80 / RAM / AY chip / sample-position state. We
     * just look up the requested subsong's INT vector and patch the
     * 3-byte JP at 0x0038 in place. On the running CPU's next IM 1
     * dispatch it picks up the new ISR.
     *
     * For tunes that genuinely need a full re-init (e.g. Star Pilot
     * song 3/3 with non-standard block layouts), the user can
     * engine_unload + engine_load to take the fresh ay_open path. */
    size_t desc = a->songs_pos + (size_t)song_index * 4;
    if (desc + 4 > a->tune_len) return 0;
    /* desc+2 is the song-data-record rel offset (signed BE16). */
    int16_t data_rel = (int16_t)((a->tune_buf[desc + 2] << 8) | a->tune_buf[desc + 3]);
    size_t song_data_pos = (size_t)((intptr_t)(desc + 2) + data_rel);
    if (song_data_pos + 14 > a->tune_len) return 0;

    /* HiReg / LoReg per-song selectors (SongData +8/+9). Some multi-song
     * tunes (e.g. Star Pilot) keep INIT/INT identical across subsongs and
     * encode the song selection entirely via these register seeds — the
     * player reads them via `IN A,(0xFFFD)` during INIT. */
    uint8_t new_hi = a->tune_buf[song_data_pos + 8];
    uint8_t new_lo = a->tune_buf[song_data_pos + 9];

    /* SP/Init/Inter live behind a signed BE16 rel pointer at SongData+10
     * (PPoints), not at SongData+10 directly. */
    int16_t pp_rel = (int16_t)((a->tune_buf[song_data_pos + 10] << 8)
                             | a->tune_buf[song_data_pos + 11]);
    size_t  pp_pos = (size_t)((intptr_t)(song_data_pos + 10) + pp_rel);
    if (pp_pos + 6 > a->tune_len) return 0;
    uint16_t new_sp    = (uint16_t)((a->tune_buf[pp_pos + 0] << 8) | a->tune_buf[pp_pos + 1]);
    uint16_t new_init  = (uint16_t)((a->tune_buf[pp_pos + 2] << 8) | a->tune_buf[pp_pos + 3]);
    uint16_t new_inter = (uint16_t)((a->tune_buf[pp_pos + 4] << 8) | a->tune_buf[pp_pos + 5]);
    if (new_inter == 0) new_inter = 0x0038;
    if (new_sp == 0)    new_sp    = 0xFFFF;

    /* Fall back to song 0's INIT if this song's INIT is 0 (common pattern
     * in multi-song .ay tunes where only song 0 has a real INIT and the
     * subsequent songs reuse the same player state). */
    if (new_init == 0 && song_index != 0) {
        size_t s0_pp_rel_pos = a->songs_pos + 2;
        if (s0_pp_rel_pos + 2 <= a->tune_len) {
            int16_t s0_rel = (int16_t)((a->tune_buf[s0_pp_rel_pos + 0] << 8)
                                     | a->tune_buf[s0_pp_rel_pos + 1]);
            size_t s0_sd = (size_t)((intptr_t)(s0_pp_rel_pos) + s0_rel);
            if (s0_sd + 12 <= a->tune_len) {
                int16_t s0_pp_rel = (int16_t)((a->tune_buf[s0_sd + 10] << 8)
                                            | a->tune_buf[s0_sd + 11]);
                size_t s0_pp_pos = (size_t)((intptr_t)(s0_sd + 10) + s0_pp_rel);
                if (s0_pp_pos + 4 <= a->tune_len) {
                    new_init = (uint16_t)((a->tune_buf[s0_pp_pos + 2] << 8)
                                        | a->tune_buf[s0_pp_pos + 3]);
                }
            }
        }
    }

    /* Always (re)load this subsong's blocks. Different subsongs in a single
     * .ay file can declare disjoint block sets — Robocop's sample-track
     * subsongs ship their player at 0x9BEC/0x9BDE plus separate sample data
     * at 0xC600/0xCD80 that the chiptune subsongs don't use. Loading is
     * additive (by-address); identical load addresses just overwrite. */
    int16_t blocks_rel = (int16_t)((a->tune_buf[song_data_pos + 12] << 8)
                                 | a->tune_buf[song_data_pos + 13]);
    size_t bp = (size_t)((intptr_t)(song_data_pos + 12) + blocks_rel);
    int max_blocks = 256;
    while (bp + 6 <= a->tune_len && max_blocks-- > 0) {
        uint16_t la = (uint16_t)((a->tune_buf[bp + 0] << 8) | a->tune_buf[bp + 1]);
        if (la == 0) break;
        uint16_t bl = (uint16_t)((a->tune_buf[bp + 2] << 8) | a->tune_buf[bp + 3]);
        int16_t  dr = (int16_t) ((a->tune_buf[bp + 4] << 8) | a->tune_buf[bp + 5]);
        size_t   da = (size_t)((intptr_t)(bp + 4) + dr);
        if (bl == 0 || da + bl > a->tune_len) break;
        ay_z80_load(&a->z80, la, a->tune_buf + da, (size_t)bl);
        bp += 6;
    }

    uint8_t new_is_sample = (new_inter == 0x001B) ? 1 : 0;

    /* Patch the RST 0x38 stub with the new Inter address. */
    uint8_t stub[3] = {
        0xC3,
        (uint8_t)(new_inter & 0xFF),
        (uint8_t)(new_inter >> 8),
    };
    ay_z80_load(&a->z80, 0x0038, stub, 3);

    /* Pre-load the per-song HiReg / LoReg seeds into the captured AY
     * register snapshot. */
    a->z80.ay_regs[1]  = new_lo;
    a->z80.ay_regs[6]  = new_hi;
    a->z80.ay_regs[7]  = new_lo;
    a->z80.ay_regs[12] = new_hi;

    /* Drive the new subsong's INIT differently depending on player type:
     *   - sample-track: seed PC/SP/A but DON'T block-call — the synchronous
     *     player will be advanced by per-sample stepping
     *   - regular: ay_z80_call as before, register A = song index */
    if (new_init != 0) {
        ((Z80_STATE*)a->z80.cpu)->registers.byte[Z80_A] = (uint8_t)song_index;
        if (new_is_sample) {
            Z80_STATE* st = (Z80_STATE*)a->z80.cpu;
            st->pc = new_init;
            st->registers.word[Z80_SP] = new_sp;
            a->z80.halted = 0;
        } else {
            ay_z80_call(&a->z80, new_init, new_sp, 2000000);
        }
    }

    a->interrupt_addr   = new_inter;
    a->init_addr        = new_init;
    a->stack_pointer    = new_sp;
    a->hi_reg           = new_hi;
    a->lo_reg           = new_lo;
    a->is_sample_track  = new_is_sample;
    a->current_song     = (uint8_t)song_index;
    return 1;
}
