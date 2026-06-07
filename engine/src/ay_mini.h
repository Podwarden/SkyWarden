/* ay_mini — minimal, integer-only AY-3-8910 / YM2149 emulator.
 *
 * Designed to run real-time on Cortex-M7 with the single-precision hardware
 * FPU sitting idle. No FIR resampler, no DC filter, no floats. Output is a
 * direct int16 mono sample per call to ay_mini_step().
 *
 * Inspired by Linus Åkesson's Craft (AVR ATmega88 chiptune): if 4 channels
 * fit on an 8-bit AVR using counters + small LUTs, 3 channels comfortably
 * fit on an M7.
 *
 * Public surface mirrors ayumi's setter names (minus pan / stereo) so the
 * engine glue is a near drop-in replacement.
 *
 * License: MIT (Anthropic; written for this project).
 */

#ifndef ENGINE_AY_MINI_H
#define ENGINE_AY_MINI_H

#include <stdint.h>

#define AY_MINI_CHIP_CLOCK_HZ 1773400   /* ZX Spectrum 128 */

typedef struct AyMini {
    /* ---- Per-channel tone ---- */
    uint16_t tone_period[3];     /* 12-bit period from registers (1..4095, 0 treated as 1) */
    int32_t  tone_counter[3];    /* counts down at chip/16, reloads from period */
    uint8_t  tone_high[3];       /* 0 or 1: current square-wave high bit */
    uint8_t  tone_disabled[3];   /* mixer R7 bit 0/1/2 (1 = disabled / pass-through HIGH) */

    /* ---- Noise ---- */
    uint8_t  noise_period;       /* 5-bit period */
    int32_t  noise_counter;
    uint32_t noise_lfsr;         /* 17-bit shift register, init to 1 */
    uint8_t  noise_disabled[3];  /* mixer R7 bit 3/4/5 */
    uint8_t  noise_out;          /* current LFSR bit-0 output (0 or 1) */

    /* ---- Per-channel volume ---- */
    uint8_t  fixed_volume[3];    /* 4-bit volume (0..15) */
    uint8_t  envelope_mode[3];   /* 1 = use envelope volume in place of fixed */

    /* ---- Envelope ---- */
    uint16_t env_period;         /* 16-bit period */
    int32_t  env_counter;
    uint8_t  env_shape;          /* 0..15, low 4 bits of R13 */
    uint8_t  env_step;           /* 0..31 step index within current shape */
    uint8_t  env_volume;         /* 4-bit current envelope output */
    uint8_t  env_holding;        /* 1 once a holding shape has reached its hold step */

    /* ---- Output config ---- */
    int      is_ym;              /* 1 = YM2149 volume curve, 0 = AY-3-8910 */
    uint32_t cycles_per_sample;  /* 16.16 fixed point: chip/16 ticks per audio sample */
    uint32_t cycle_accum;        /* fractional accumulator (16.16) */

    /* ---- DC-removal high-pass filter state ----
     * One-pole IIR HP at ~10 Hz cutoff (44.1 kHz sample rate). Removes the
     * single-rail DC bias dynamically — handles silence-to-tone transitions
     * cleanly without relying on the Playdate's analog AC-coupling. */
    int32_t  hpf_prev_x;
    int32_t  hpf_prev_y;

    /* ---- Anti-alias low-pass filter state (2-pole Butterworth biquad) ----
     * Cuts harmonics above ~10 kHz where most residual aliasing energy lives,
     * with a 12 dB/octave knee (vs the box-car's gentle 6 dB/octave sinc).
     * Combined the two filters give effective ~18 dB/octave above 10 kHz.
     * Direct-form-I biquad keeps the four state values trivially manageable. */
    int32_t  lpf_x1, lpf_x2;     /* x[n-1], x[n-2] */
    int32_t  lpf_y1, lpf_y2;     /* y[n-1], y[n-2] */
} AyMini;

/* Configure / reset. sample_rate must be > 0. is_ym selects the volume curve. */
void ay_mini_init(AyMini* c, int is_ym, int sample_rate);

void ay_mini_set_tone(AyMini* c, int ch, int period);                   /* 12-bit */
void ay_mini_set_noise(AyMini* c, int period);                          /* 5-bit  */
void ay_mini_set_mixer(AyMini* c, int ch, int t_off, int n_off, int e_on);
void ay_mini_set_volume(AyMini* c, int ch, int volume);                 /* 0..15  */
void ay_mini_set_envelope(AyMini* c, int period);                       /* 16-bit */
void ay_mini_set_envelope_shape(AyMini* c, int shape);                  /* 0..15  */

/* Render exactly one mono int16 audio sample. Internal counters advance by
 * `cycles_per_sample` chip-divide-16 ticks (≈2.5 at 44.1 kHz / 1.77 MHz). */
int16_t ay_mini_step(AyMini* c);

#endif
