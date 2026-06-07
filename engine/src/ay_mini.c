/* ay_mini — minimal integer-only AY-3-8910 / YM2149 emulator.
 *
 * See ay_mini.h for the rationale (Craft-style, no resampler, no float).
 *
 * Per-sample cost (44.1 kHz / 1.77 MHz chip): ~2.5 chip/16 ticks → about
 *   ~5 ops × 2.5 = ~12 inner-loop ops plus 3 channel sums and 3 LUT reads.
 * Estimated ≤ 60 instructions on Cortex-M7 ≈ <0.5 µs/sample at 200 MHz
 * (compared to ~66 µs/sample for ayumi's FIR).
 *
 * License: MIT.
 */

#include "ay_mini.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * Volume tables (16 entries, scaled so 3 channels at max amplitude sum to
 * roughly the int16 max). Both tables are taken from ayumi's float DAC
 * tables and scaled by 0x2AAA (so per-channel max = 0x2AAA, sum-of-three
 * max = 0x7FFE, comfortably within INT16_MAX = 0x7FFF).
 *
 * The shape of each curve preserves ayumi's perceptual loudness profile:
 *   YM2149: brighter, finer steps at the top
 *   AY-3-8910: simpler logarithmic
 * ------------------------------------------------------------------------- */

/* YM2149 curve, scaled by 0x2AAA so 3-channel sum max ≈ 0x7FFE ≤ INT16_MAX.
 * Derived from ayumi's float YM_DAC_TABLE (32 entries, picking the brighter
 * slot of each pair which corresponds to 4-bit fixed volume). */
static const uint16_t ym_vol_table[16] = {
    0x0000, 0x0056, 0x009C, 0x00E0, 0x014D, 0x01C4, 0x028E, 0x0367,
    0x04DC, 0x067F, 0x0941, 0x0C4D, 0x1185, 0x1761, 0x2129, 0x2AAA,
};

/* AY-3-8910 curve, scaled by 0x2AAA. Derived from ayumi's AY_DAC_TABLE. */
static const uint16_t ay_vol_table[16] = {
    0x0000, 0x006D, 0x009E, 0x00E6, 0x014F, 0x01F1, 0x02C0, 0x0495,
    0x0566, 0x08BE, 0x0C77, 0x0FE8, 0x1503, 0x1B1A, 0x225E, 0x2AAA,
};

#define AY_VOL(i)  (ay_vol_table[(i) & 0x0F])
#define YM_VOL(i)  (ym_vol_table[(i) & 0x0F])

/* -------------------------------------------------------------------------
 * Envelope shape table: 16 shapes × 32 steps, each entry a 4-bit volume.
 *
 * Building blocks (each 16 long):
 *   DOWN: 15..0
 *   UP:   0..15
 *   ZERO: all 0
 *   ONE:  all 15
 *
 * Continue/Attack/Alternate/Hold (R13 bits 3/2/1/0) selection per the
 * AY-3-8910 datasheet:
 *
 *   CONT  HOLD  ALT   ATT
 *   0     x     x     0   →  DOWN  + ZERO   (shapes 0..3)
 *   0     x     x     1   →  UP    + ZERO   (shapes 4..7)
 *   1     0     0     0   →  DOWN  + DOWN   (shape 8)
 *   1     1     0     0   →  DOWN  + ZERO   (shape 9)
 *   1     0     1     0   →  DOWN  + UP     (shape 10)
 *   1     1     1     0   →  DOWN  + ONE    (shape 11)
 *   1     0     0     1   →  UP    + UP     (shape 12)
 *   1     1     0     1   →  UP    + ONE    (shape 13)
 *   1     0     1     1   →  UP    + DOWN   (shape 14)
 *   1     1     1     1   →  UP    + ZERO   (shape 15)
 *
 * For non-continuous (CONT=0) shapes and CONT=1+HOLD=1 shapes, after step 31
 * the envelope holds at the value of step 31. For continuous shapes (8, 10,
 * 12, 14), step wraps 31→0.
 * ------------------------------------------------------------------------- */

static const uint8_t env_table[16][32] = {
    /* 0: \__ */ {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 1: \__ */ {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 2: \__ */ {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 3: \__ */ {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 4: /__ */ {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 5: /__ */ {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 6: /__ */ {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 7: /__ */ {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 8: \\\ */ {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0, 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0},
    /* 9: \__ */ {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /*10: \/  */ {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
    /*11: \¯  */ {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0, 15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15},
    /*12: /// */ {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
    /*13: /¯  */ {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15},
    /*14: /\  */ {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0},
    /*15: /__ */ {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

/* For each shape: 1 if the shape holds at step 31, 0 if it wraps. */
static const uint8_t env_holds[16] = {
    1,1,1,1,1,1,1,1,  /* 0..7  : all hold */
    0,1,0,1,           /* 8 wrap, 9 hold, 10 wrap, 11 hold */
    0,1,0,1,           /* 12 wrap, 13 hold, 14 wrap, 15 hold */
};

/* -------------------------------------------------------------------------
 * Setters
 * ------------------------------------------------------------------------- */

void ay_mini_init(AyMini* c, int is_ym, int sample_rate) {
    memset(c, 0, sizeof(*c));
    c->is_ym       = is_ym ? 1 : 0;
    c->noise_lfsr  = 1;
    c->env_period  = 1;
    c->noise_period = 1;
    for (int ch = 0; ch < 3; ++ch) {
        c->tone_period[ch]  = 1;
        c->tone_counter[ch] = 1;
        c->tone_disabled[ch]  = 1;   /* default mixer: everything off (R7 = 0x3F) */
        c->noise_disabled[ch] = 1;
    }
    c->noise_counter = 1;
    c->env_counter   = 1;
    /* cycles_per_sample is in 16.16 fixed point, units of "chip/8" ticks
     * per audio sample.
     *
     * The AY-3-8910 datasheet's tone-frequency formula is
     *     f = chip_clock / (16 × Tp)
     * where Tp is the 12-bit tone-period register. Our chip step toggles
     * the output on every counter underflow, which means we need TWO
     * underflows per full-cycle period — so the counter must tick at
     * chip_clock/8 (not /16) to make Tp=252 give 440 Hz, as every
     * chiptune file in the wild expects. (Stepping at /16 produced exactly
     * half-pitch playback for all PSG/VTX/YM tunes, masked by a unit test
     * that used Tp=126 instead of the canonical 252.) */
    if (sample_rate <= 0) sample_rate = 44100;
    c->cycles_per_sample =
        (uint32_t)((uint64_t)AY_MINI_CHIP_CLOCK_HZ * 65536u
                   / (uint64_t)(8u * (uint32_t)sample_rate));
    c->cycle_accum = 0;
    /* Initial envelope volume = step 0 of shape 0 (top of ramp-down). */
    c->env_step    = 0;
    c->env_volume  = env_table[0][0];
    c->env_holding = 0;
    /* HP + LP filter state — memset(0) already zeroed but be explicit. */
    c->hpf_prev_x  = 0;
    c->hpf_prev_y  = 0;
    c->lpf_x1 = c->lpf_x2 = c->lpf_y1 = c->lpf_y2 = 0;
}

void ay_mini_set_tone(AyMini* c, int ch, int period) {
    if ((unsigned)ch >= 3u) return;
    period &= 0x0FFF;
    if (period == 0) period = 1;     /* AY treats period 0 as 1 */
    c->tone_period[ch] = (uint16_t)period;
}

void ay_mini_set_noise(AyMini* c, int period) {
    period &= 0x1F;
    if (period == 0) period = 1;
    c->noise_period = (uint8_t)period;
}

void ay_mini_set_mixer(AyMini* c, int ch, int t_off, int n_off, int e_on) {
    if ((unsigned)ch >= 3u) return;
    c->tone_disabled[ch]  = t_off ? 1 : 0;
    c->noise_disabled[ch] = n_off ? 1 : 0;
    c->envelope_mode[ch]  = e_on  ? 1 : 0;
}

void ay_mini_set_volume(AyMini* c, int ch, int volume) {
    if ((unsigned)ch >= 3u) return;
    c->fixed_volume[ch] = (uint8_t)(volume & 0x0F);
}

void ay_mini_set_envelope(AyMini* c, int period) {
    period &= 0xFFFF;
    if (period == 0) period = 1;
    c->env_period = (uint16_t)period;
}

void ay_mini_set_envelope_shape(AyMini* c, int shape) {
    c->env_shape   = (uint8_t)(shape & 0x0F);
    c->env_step    = 0;
    c->env_counter = c->env_period;
    c->env_volume  = env_table[c->env_shape][0];
    c->env_holding = 0;
}

/* -------------------------------------------------------------------------
 * Inner step: render one int16 sample.
 *
 * We use a 16.16 fixed-point accumulator. cycles_per_sample is "chip/16
 * ticks per audio sample" left-shifted by 16. At 44.1 kHz / 1.77 MHz this
 * is ~2.5 ticks/sample so the inner loop runs 2 or 3 times.
 *
 * Anti-aliasing strategy — box-car averaging over the chip ticks of one
 * audio sample period. The chip's actual output is a square wave that
 * toggles between two levels at a chip tick; sampling only the LAST level
 * (nearest-neighbour) throws away the transitions and folds high-frequency
 * harmonics back as audible aliasing ("digital noise"). Averaging the
 * output across all ticks of the period applies a box-car (sinc-magnitude)
 * lowpass that has nulls at multiples of the audio rate — exactly where
 * the alias mirrors land. Cheap, math-only, no FIR table needed.
 *
 * DC removal — single-rail output (0..0x7FFE) has a tone-amplitude-dependent
 * DC bias. The Playdate's analog stage is AC-coupled and will filter it,
 * but a one-pole IIR HP here removes the bias dynamically so silence-to-
 * tone transitions don't produce "thumps" while the cap charges.
 *
 * HP filter:  y[n] = a * (y[n-1] + x[n] - x[n-1])
 * where a ≈ 0.9988 for ~8 Hz cutoff at 44.1 kHz. In Q16 fixed point:
 *   a × 65536 ≈ 65458   (we use 65460 for clean shift math)
 * ------------------------------------------------------------------------- */

#define AY_MINI_HPF_A_Q16 65460   /* one-pole HP coefficient × 65536 (~8 Hz @ 44.1 kHz) */

/* 2-pole LOW-PASS biquad, fc = 10 kHz at fs = 44.1 kHz, **critically damped**.
 *
 * Originally used Butterworth (Q=0.707) which has a "maximally flat" magnitude
 * but a slight overshoot on step inputs (~4% peak overshoot, ~6% undershoot
 * over a few samples). Side-by-side render vs reference ayumi confirmed that
 * exact overshoot-then-undershoot pattern at every tone-square-wave edge:
 *
 *   sample i-1: -0.076  (last of low half)
 *   sample i:   +0.057  (transition)
 *   sample i+1: +0.152
 *   sample i+2: +0.158  ← overshoot
 *   sample i+3: +0.143  ← undershoot
 *   sample i+4: +0.139  (settles)
 *
 * At 440 Hz × 2 transitions/cycle = 880 of these pings per second, which is
 * the audible "click-click" the user reported.
 *
 * Q = 0.5 gives a *critically damped* response — monotonic step (no overshoot,
 * no undershoot). Same -3 dB point and same 12 dB/octave rolloff; the only
 * difference is no ringing at edges, which is exactly what we want for a
 * square-wave-heavy signal.
 *
 * RBJ formulas with Q=0.5:
 *   ω₀ = 2π·10000/44100 ≈ 1.4248
 *   cos(ω₀) ≈ 0.1465, sin(ω₀) ≈ 0.9892, α = sin/(2·0.5) = sin ≈ 0.9892
 *   b0 = (1-cos)/2 ≈ 0.4268, b1 = 1-cos ≈ 0.8536, b2 ≈ 0.4268
 *   a0 = 1+α ≈ 1.9892, a1 = -2cos ≈ -0.2930, a2 = 1-α ≈ 0.0108
 * Normalized by a0, scaled to Q15:
 *   B0 ≈ 0.2146 → 7032
 *   B1 ≈ 0.4291 → 14063
 *   B2 ≈ 0.2146 → 7032
 *   A1 ≈ -0.1473 → -4827
 *   A2 ≈ 0.00543 → 178
 */
#define AY_MINI_LPF_B0_Q15  7032
#define AY_MINI_LPF_B1_Q15  14063
#define AY_MINI_LPF_B2_Q15  7032
#define AY_MINI_LPF_A1_Q15  (-4827)
#define AY_MINI_LPF_A2_Q15  178

/* Final-stage gain to compensate for amplitude lost when HP centers the
 * single-rail signal on zero. Reference ayumi (heavily band-limited) renders
 * the same square wave at peak ~0.10; we render at peak ~0.30 with gain=2.0
 * — 3× too loud. Drop to 1.5× to land closer to ayumi's level while still
 * comfortably louder than the bare HP-only output (peak 0.07). */
#define AY_MINI_GAIN_Q8 384    /* ×1.5 in Q8 fixed point */

int16_t ay_mini_step(AyMini* c) {
    c->cycle_accum += c->cycles_per_sample;
    uint32_t n_ticks = c->cycle_accum >> 16;
    c->cycle_accum &= 0x0000FFFFu;
    /* Defensive: if cycles_per_sample is configured such that we ever get 0
     * ticks for an audio sample (shouldn't with our 44.1/1.77 ratio of
     * ~2.5), output silence rather than dividing by zero below. */
    if (n_ticks == 0) return 0;

    int32_t accumulator = 0;

    for (uint32_t t = 0; t < n_ticks; ++t) {
        /* === advance chip state by one chip/16 tick === */

        /* Three tone counters, fully unrolled. */
        if (--c->tone_counter[0] <= 0) {
            c->tone_counter[0] = c->tone_period[0];
            c->tone_high[0]   ^= 1;
        }
        if (--c->tone_counter[1] <= 0) {
            c->tone_counter[1] = c->tone_period[1];
            c->tone_high[1]   ^= 1;
        }
        if (--c->tone_counter[2] <= 0) {
            c->tone_counter[2] = c->tone_period[2];
            c->tone_high[2]   ^= 1;
        }

        /* Noise counter + 17-bit Galois LFSR (taps at bit 0 and bit 3). */
        if (--c->noise_counter <= 0) {
            c->noise_counter = (int32_t)c->noise_period;
            uint32_t bit = (c->noise_lfsr ^ (c->noise_lfsr >> 3)) & 1u;
            c->noise_lfsr = (c->noise_lfsr >> 1) | (bit << 16);
            c->noise_out  = (uint8_t)(c->noise_lfsr & 1u);
        }

        /* Envelope counter steps every env_period chip/16 ticks. */
        if (--c->env_counter <= 0) {
            c->env_counter = (int32_t)c->env_period;
            if (!c->env_holding) {
                uint8_t s = c->env_step;
                if (s < 31) {
                    c->env_step = s + 1;
                } else if (env_holds[c->env_shape]) {
                    c->env_holding = 1;       /* stay at step 31's value */
                } else {
                    c->env_step = 0;          /* wrap for continuous shapes */
                }
                c->env_volume = env_table[c->env_shape][c->env_step];
            }
        }

        /* === compute output at THIS tick (for box-car averaging) === */
        int32_t cur = 0;
        for (int ch = 0; ch < 3; ++ch) {
            uint8_t tone  = c->tone_disabled[ch]  ? 1 : c->tone_high[ch];
            uint8_t noise = c->noise_disabled[ch] ? 1 : c->noise_out;
            if (tone & noise) {
                uint8_t v = c->envelope_mode[ch] ? c->env_volume
                                                 : c->fixed_volume[ch];
                cur += c->is_ym ? (int32_t)YM_VOL(v) : (int32_t)AY_VOL(v);
            }
        }
        accumulator += cur;
    }

    /* Box-car average — kills the aliasing mirrors at audio-rate multiples. */
    int32_t x = accumulator / (int32_t)n_ticks;   /* still in 0..0x7FFE range */

    /* One-pole IIR HP at ~8 Hz: y = (a * (y_prev + x - x_prev) + 0x8000) >> 16
     * Rounding by adding 0x8000 before the shift avoids slow drift. */
    int64_t hp = (int64_t)AY_MINI_HPF_A_Q16
               * (int64_t)(c->hpf_prev_y + x - c->hpf_prev_x);
    int32_t hp_out = (int32_t)((hp + 0x8000) >> 16);
    c->hpf_prev_x = x;
    c->hpf_prev_y = hp_out;

    /* 2-pole Butterworth low-pass at 10 kHz. Direct-form-I biquad in Q15.
     * Suppresses the residual aliasing above 10 kHz that the box-car only
     * partially attenuates. */
    int64_t lp = (int64_t)AY_MINI_LPF_B0_Q15 * hp_out
               + (int64_t)AY_MINI_LPF_B1_Q15 * c->lpf_x1
               + (int64_t)AY_MINI_LPF_B2_Q15 * c->lpf_x2
               - (int64_t)AY_MINI_LPF_A1_Q15 * c->lpf_y1
               - (int64_t)AY_MINI_LPF_A2_Q15 * c->lpf_y2;
    int32_t lp_out = (int32_t)((lp + 0x4000) >> 15);
    c->lpf_x2 = c->lpf_x1; c->lpf_x1 = hp_out;
    c->lpf_y2 = c->lpf_y1; c->lpf_y1 = lp_out;

    /* Final ×2 gain to compensate for amplitude lost across the HP filter
     * (HP halves a single-rail square's peak to bipolar ±A/2). */
    int32_t y = (lp_out * AY_MINI_GAIN_Q8) >> 8;

    /* Clamp to int16. */
    if (y >  32767) y =  32767;
    if (y < -32768) y = -32768;
    return (int16_t)y;
}
