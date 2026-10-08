#ifndef ENGINE_AY_Z80_GLUE_H
#define ENGINE_AY_Z80_GLUE_H

#include <stdint.h>
#include <stddef.h>

/* Thin wrapper around the vendored anotherlin/z80emu instruction-stepped Z80:
 *   - 176 KB virtual Z80 RAM, split into 11 × 16 KB banks (single heap alloc).
 *     Logical→physical mapping mirrors ZX Spectrum 128K paging, but with
 *     DEDICATED physical banks for the three fixed lower pages so they never
 *     alias the switchable upper-page banks:
 *       0x0000–0x3FFF → physical bank 0  (fixed lower-0; "ROM" slot)
 *       0x4000–0x7FFF → physical bank 1  (fixed lower-1; "screen" slot)
 *       0x8000–0xBFFF → physical bank 2  (fixed lower-2)
 *       0xC000–0xFFFF → physical bank (3 + current_upper_bank), i.e. 3..10
 *     current_upper_bank (0..7) is the low 3 bits of the last OUT 0x7FFD.
 *   - Z80_STATE struct (~244 B on 32-bit; also heap to fit AyFile <= 256 B)
 *   - 14-byte AY register snapshot updated in real time as the Z80 player
 *     code executes OUT (#FFFD)/(#BFFD) instructions.
 *
 * Why z80emu vs the previous floooh/chips cycle-stepped emulator?
 * Chiptune playback only needs OUT-port capture, not per-cycle bus accuracy.
 * Instruction-stepped emulators run many fewer host instructions per
 * emulated Z80 cycle — on Cortex-M7 the previous cycle-stepped Z80
 * benchmarked at ~560 kHz effective vs the 3.5 MHz target.
 *
 * The vendored player + ZX Spectrum interrupt-driven music players work by:
 *   1. Loading binary blobs at fixed addresses (via ay_z80_load).
 *   2. Calling INIT once with a sentinel-return trick (ay_z80_call).
 *   3. Per 50 Hz audio frame: asserting INT and running 70000 T-states
 *      (ay_z80_interrupt_frame, or the spread-across-samples step pair).
 *      The Z80 enters IM 1 (RST 0x38) or jumps to the .ay file's
 *      interrupt_addr; the player updates AY registers via OUT writes;
 *      control returns; we snapshot regs.
 */

typedef struct AyZ80 {
    void*    cpu;                    /* heap: Z80_STATE (244 B); kept opaque here */
    uint8_t* ram_banks;              /* heap: 180224 bytes = 11 × 16 KB, single alloc */
    uint8_t  current_upper_bank;     /* low 3 bits of last OUT 0x7FFD; 0..7; default 0 */
    uint8_t  _pad[3];                /* alignment padding */
    uint8_t  ay_regs[16];      /* current AY register state, R0..R15 */
    uint8_t  ay_selected;      /* most-recent register selected via OUT #FFFD */
    uint8_t  int_pending;      /* 1 = caller asserted INT; we deliver next step */
    uint8_t  r13_fresh;        /* 1 if Z80 wrote R13 since last consume_regs */
    uint8_t  halted;           /* 1 = CPU is at HALT, waiting for INT */
    uint32_t diag_out_writes;  /* total OUTs that matched an AY port (any reg) */
    uint32_t diag_r13_writes;  /* OUTs to selected reg = 13 (envelope shape) */
    uint32_t diag_int_acks;    /* number of INTs the CPU has accepted */
} AyZ80;

_Static_assert(sizeof(AyZ80) <= 200, "AyZ80 struct unexpectedly large (> 200 bytes)");

typedef struct AyZ80Diag {
    uint32_t out_writes;
    uint32_t r13_writes;
    uint32_t int_acks;
} AyZ80Diag;

void ay_z80_get_diag(const AyZ80* z, AyZ80Diag* out);
void ay_z80_reset_diag(AyZ80* z);

int  ay_z80_init(AyZ80* z);
void ay_z80_close(AyZ80* z);
void ay_z80_load(AyZ80* z, uint16_t addr, const uint8_t* data, size_t len);

/* Run a Z80 subroutine at `addr` with the given SP. Pushes a sentinel return
 * address (0x0000) onto the Z80 stack; runs until Z80 PC == 0 or
 * max_tstates elapsed. Used to invoke .ay INIT routine once. */
void ay_z80_call(AyZ80* z, uint16_t addr, uint16_t sp, int max_tstates);

/* Assert maskable interrupt + run Z80 for N T-states. Used for 50 Hz player
 * frame service when running one full frame in one call (test paths). */
void ay_z80_interrupt_frame(AyZ80* z, int tstates);

/* Run the Z80 for ~n_ticks T-states. May overshoot by part of one
 * instruction (z80emu completes whatever opcode it is on). */
void ay_z80_step(AyZ80* z, int n_ticks);

/* Set INT-pending, then step n_ticks_after T-states. The Z80 takes the
 * INT at the very next instruction boundary if IFF1 is set; otherwise the
 * pending flag persists until IFF1 is set (e.g. by EI). */
void ay_z80_assert_int_then_step(AyZ80* z, int n_ticks_after);

/* Read the current AY register snapshot (R0..R13 musical; R14/R15 unused). */
static inline const uint8_t* ay_z80_get_regs(const AyZ80* z) {
    return z->ay_regs;
}

/* Like ay_z80_get_regs but presents R13 as 0xFF unless it was written since
 * the last call to this function. Stream-format protocol: 0xFF = "no envelope
 * change this frame". Modifies internal freshness flag. */
void ay_z80_consume_regs(AyZ80* z, uint8_t* regs_out);

/* ---- Bus thunks called from z80emu.c (see third_party/z80emu/z80user.h) ---- */

uint8_t ay_z80_bus_read(AyZ80* z, uint16_t addr);
void    ay_z80_bus_write(AyZ80* z, uint16_t addr, uint8_t v);
uint8_t ay_z80_bus_in(AyZ80* z, uint16_t port);
void    ay_z80_bus_out(AyZ80* z, uint16_t port, uint8_t v);

#endif
