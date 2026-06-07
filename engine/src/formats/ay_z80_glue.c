/* Glue between the engine and anotherlin/z80emu (instruction-stepped Z80).
 *
 * z80emu's user-customization point is third_party/z80emu/z80user.h, which we
 * have rewritten to call out to the `ay_z80_bus_*` thunks defined in this
 * file via plain externs.  Those thunks handle:
 *   - 176 KB RAM in z->ram_banks (11 × 16 KB banks: 3 fixed + 8 switchable)
 *   - ZX Spectrum AY port decoding for IN/OUT
 *   - AY register snapshot bookkeeping (incl. R13-fresh + diag counters)
 *
 * Note on Z80_STATE pointers: Z80Reset populates internal pointer tables
 * pointing INTO the state struct. The cpu is heap-allocated here and never
 * moved, so those pointers stay valid for the lifetime of the AyZ80.
 */

#include "ay_z80_glue.h"
#include "z80emu.h"

#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(Z80_STATE) < 4096, "Z80_STATE struct is unexpectedly large");

/* Resolve a 16-bit Z80 logical address to a pointer in our 11-bank array.
 *
 * Physical layout (each slot is 16 KB = 16384 bytes):
 *   physical bank 0  → fixed lower slot 0x0000–0x3FFF ("ROM" page)
 *   physical bank 1  → fixed lower slot 0x4000–0x7FFF ("screen" page)
 *   physical bank 2  → fixed lower slot 0x8000–0xBFFF
 *   physical bank 3  → switchable upper bank 0  (current_upper_bank == 0)
 *   physical bank 4  → switchable upper bank 1
 *   ...
 *   physical bank 10 → switchable upper bank 7  (current_upper_bank == 7)
 *
 * Using dedicated physical banks for the three fixed lower pages ensures they
 * never alias the switchable upper page even when current_upper_bank is 0, 1,
 * or 2.  This mirrors the ZX Spectrum 128K's separate ROM chip for the lower
 * 16 KB.  One inlined branch + index per call. */
__attribute__((always_inline)) static inline uint8_t*
ay_z80_resolve(AyZ80* z, uint16_t addr) {
    unsigned phys;
    if      (addr < 0x4000u) phys = 0u;
    else if (addr < 0x8000u) phys = 1u;
    else if (addr < 0xC000u) phys = 2u;
    else                     phys = 3u + (unsigned)(z->current_upper_bank & 0x07u);
    return &z->ram_banks[phys * 16384u + (addr & 0x3FFFu)];
}

/* ---- ZX Spectrum AY port decoding ---- */
static int is_ay_select_port(uint16_t addr) {
    return (addr & 0xC002u) == 0xC000u;   /* A15=1, A14=1, A1=0 (e.g. 0xFFFD) */
}
static int is_ay_write_port(uint16_t addr) {
    return (addr & 0xC002u) == 0x8000u;   /* A15=1, A14=0, A1=0 (e.g. 0xBFFD) */
}

/* ---- z80emu bus callbacks (declared in z80user.h) ---- */

uint8_t ay_z80_bus_read(AyZ80* z, uint16_t addr) {
    return *ay_z80_resolve(z, addr);
}

void ay_z80_bus_write(AyZ80* z, uint16_t addr, uint8_t v) {
    *ay_z80_resolve(z, addr) = v;
}

uint8_t ay_z80_bus_in(AyZ80* z, uint16_t port) {
    /* AY register read — only at FFFD-aliased ports. Above R13 returns
     * 0xFF (idle ZX bus floats high). Non-AY ports return 0xFF. */
    if (is_ay_select_port(port)) {
        return (z->ay_selected < 14) ? z->ay_regs[z->ay_selected] : 0xFF;
    }
    return 0xFF;
}

void ay_z80_bus_out(AyZ80* z, uint16_t port, uint8_t v) {
    /* Spectrum 128K paging port: A15=0, A1=0 selects 0x7FFD. Bits 0-2 of the
     * written value choose the RAM bank mapped at 0xC000-0xFFFF. The other
     * bits (3 = screen page, 4 = ROM page, 5 = paging-disable latch) don't
     * affect chiptune playback. A single OUT can't be both 7FFD and an AY
     * port: AY ports have A15=1, 7FFD has A15=0. */
    if ((port & 0x8002u) == 0x0000u) {
        z->current_upper_bank = (uint8_t)(v & 0x07u);
        return;
    }
    if (is_ay_select_port(port)) {
        z->ay_selected = v & 0x0F;
    } else if (is_ay_write_port(port)) {
        if (z->ay_selected < 16) {
            z->ay_regs[z->ay_selected] = v;
            z->diag_out_writes++;
            if (z->ay_selected == 13) {
                z->diag_r13_writes++;
                z->r13_fresh = 1;
            }
        }
    }
}

/* ---- Lifecycle ---- */

int ay_z80_init(AyZ80* z) {
    memset(z, 0, sizeof(*z));
    z->ram_banks = (uint8_t*)calloc(11 * 16384, 1);  /* 180224 bytes, 11 × 16 KB */
    if (!z->ram_banks) return 0;
    /* current_upper_bank already 0 from memset above. */
    z->cpu = calloc(1, sizeof(Z80_STATE));
    if (!z->cpu) { free(z->ram_banks); z->ram_banks = NULL; return 0; }
    Z80_STATE* st = (Z80_STATE*)z->cpu;
    Z80Reset(st);
    /* Default to IM 1 + interrupts enabled. The Project AY format expects
     * .ay players to run inside a Spectrum context where IFF1 is already 1
     * by the time the music starts (the OS / ROM handles power-up). Many
     * stripped .ay rips (Robin of the Wood, etc.) omit the IM/EI setup in
     * their own INIT code because real-hardware playback never needed it.
     *
     * Setting these post-Reset gives such rips a chance to play. Tunes
     * that actively call DI or IM 2 inside their INIT override these
     * values normally — no harm done. */
    st->iff1 = 1;
    st->iff2 = 1;
    st->im   = 1;
    return 1;
}

void ay_z80_close(AyZ80* z) {
    if (z->cpu) { free(z->cpu); z->cpu = NULL; }
    if (z->ram_banks) { free(z->ram_banks); z->ram_banks = NULL; }
}

void ay_z80_load(AyZ80* z, uint16_t addr, const uint8_t* data, size_t len) {
    if (!z->ram_banks || !data || len == 0) return;
    for (size_t i = 0; i < len; ++i) {
        *ay_z80_resolve(z, (uint16_t)(addr + i)) = data[i];
    }
}

/* ---- Sub-routine call with sentinel-return trick ---- */

void ay_z80_call(AyZ80* z, uint16_t addr, uint16_t sp, int max_tstates) {
    if (!z->cpu || !z->ram_banks) return;
    Z80_STATE* st = (Z80_STATE*)z->cpu;

    /* Install an EI; HALT; JR -4 trampoline at 0x0000-0x0003. Stays in place
     * after INIT (no restore at end of this function).
     *
     * Why a trampoline instead of a single HALT byte:
     *
     * Two .ay-player conventions exist for "INIT done" semantics:
     *   (a) Well-behaved: INIT enters its own EI;HALT;<work> main loop and
     *       never returns. z80emu stops at the HALT in player address space.
     *   (b) Stripped-rip (Robocop, Cybernoid 2, ...): INIT ends with plain
     *       RET. The Z80 pops our sentinel 0x0000 from the stack and jumps
     *       to PC=0x0000. We need it to land in a stable "wait for INT"
     *       loop, not NOP-slide through 64 KB of garbage memory.
     *
     * The 4-byte trampoline at 0x0000 (EI / HALT / JR -4) handles case (b):
     *   PC=0x0000  EI       enables IFF1
     *   PC=0x0001  HALT     CPU halts; on INT, Z80Interrupt pushes 0x0002
     *   PC=0x0002  JR -4    after RETI returns here, jump back to EI
     *
     * Case (a) is unaffected because the player never RETs to 0x0000 —
     * z80emu stops at the player's own HALT inside its code area.
     *
     * Some .ay players' ISRs end with plain RET (not RETI), leaving IFF1=0.
     * The trampoline's leading EI re-arms IFF1 every loop, so the next 50 Hz
     * INT fires correctly. (The runtime `ay_z80_assert_int_then_step` also
     * force-sets IFF1 as belt-and-braces, but this trampoline is what makes
     * RET-style INITs progress in the first place.) */
    *ay_z80_resolve(z, 0x0000) = 0xFB;   /* EI */
    *ay_z80_resolve(z, 0x0001) = 0x76;   /* HALT */
    *ay_z80_resolve(z, 0x0002) = 0x18;   /* JR -4 (back to 0x0000 EI) */
    *ay_z80_resolve(z, 0x0003) = 0xFC;

    sp -= 2;
    *ay_z80_resolve(z, sp)     = 0x00;
    *ay_z80_resolve(z, (uint16_t)(sp + 1)) = 0x00;
    st->registers.word[Z80_SP] = sp;
    st->pc = addr;
    z->halted = 0;
    st->status = 0;

    int total = 0;
    const int CHUNK = 200;
    while (total < max_tstates) {
        int got = Z80Emulate(st, CHUNK, z);
        total += got;
        if (got <= 0) break;
        if (st->status == Z80_STATUS_HALT) {
            /* z80emu leaves PC at HALT+1 (matches real Z80 INT semantics). */
            if (st->pc <= 3) {
                /* Sentinel trampoline reached — INIT RET'd cleanly. CPU is
                 * now halted at PC=0x0002, ready for the first INT. */
                z->halted = 1;
            } else {
                /* Player entered its own HALT loop — INIT is "done" in the
                 * sense that the player is now waiting for INT. */
                z->halted = 1;
            }
            break;
        }
        if (st->pc <= 1) break;     /* defensive: RET landed without HALT step */
    }

    /* Do NOT restore the original byte at 0x0000. The trampoline stays
     * resident; it's the "main loop" for RET-style players. Case-(a) players
     * never execute from low memory so they don't care what's at 0x0000. */
    z->int_pending = 0;
}

/* ---- Stepping ---- */

/* Try to deliver a pending IM 1 interrupt and run cycles.
 *
 * State machine:
 *   - halted=0: run Z80Emulate(budget). If returns with status=HALT, set
 *     halted=1, roll PC back to HALT byte.
 *   - halted=1 + int_pending=1: call Z80Interrupt. If accepted (IFF1=1),
 *     halted=0, run remaining cycles via Z80Emulate.
 *   - halted=1 + int_pending=0: consume budget (no Z80 work; CPU idle). */
static void glue_run(AyZ80* z, int n_ticks) {
    Z80_STATE* st = (Z80_STATE*)z->cpu;
    int budget = n_ticks;

    if (z->halted && z->int_pending) {
        int int_cycles = Z80Interrupt(st, 0xFF, z);
        if (int_cycles > 0) {
            z->int_pending = 0;
            z->diag_int_acks++;
            z->halted = 0;
            budget -= int_cycles;
            if (budget < 0) budget = 0;
        } else {
            /* Maskable INT rejected (IFF1=0). With CPU halted and IFF1=0
             * this would deadlock on real hardware too; return so the caller
             * can keep trying. */
            return;
        }
    } else if (!z->halted && z->int_pending) {
        int int_cycles = Z80Interrupt(st, 0xFF, z);
        if (int_cycles > 0) {
            z->int_pending = 0;
            z->diag_int_acks++;
            budget -= int_cycles;
            if (budget < 0) budget = 0;
        }
        /* If rejected, leave int_pending set; we'll retry next step. */
    }

    if (z->halted) {
        /* No INT to wake us — just consume cycles. PC stays at HALT. */
        return;
    }

    if (budget > 0) {
        st->status = 0;
        (void)Z80Emulate(st, budget, z);
        if (st->status == Z80_STATUS_HALT) {
            /* z80emu leaves PC at HALT+1, which is exactly what we need:
             * Z80Interrupt will later push PC=HALT+1 onto the stack, and
             * RETI will resume at the instruction after HALT (matching
             * real-Z80 behaviour). DO NOT roll PC back. */
            z->halted = 1;
        }
    }
}

void ay_z80_step(AyZ80* z, int n_ticks) {
    if (!z->cpu || !z->ram_banks) return;
    if (n_ticks <= 0) return;
    glue_run(z, n_ticks);
}

void ay_z80_assert_int_then_step(AyZ80* z, int n_ticks_after) {
    if (!z->cpu || !z->ram_banks) return;
    /* Belt-and-braces: re-arm IFF1 only when halted=1 (the CPU physically
     * cannot EI itself out). Running code that intentionally DI'd stays
     * DI'd. The EI;HALT;JR-4 trampoline that ay_z80_call installs at
     * 0x0000 handles IFF1 re-enabling for RET-style INITs. */
    if (z->halted) {
        ((Z80_STATE*)z->cpu)->iff1 = 1;
        ((Z80_STATE*)z->cpu)->iff2 = 1;
    }
    z->int_pending = 1;
    glue_run(z, n_ticks_after);
}

void ay_z80_interrupt_frame(AyZ80* z, int tstates) {
    if (!z->cpu || !z->ram_banks) return;
    if (z->halted) {
        ((Z80_STATE*)z->cpu)->iff1 = 1;
        ((Z80_STATE*)z->cpu)->iff2 = 1;
    }
    z->int_pending = 1;
    glue_run(z, tstates);
}

/* ---- Snapshots / diag ---- */

void ay_z80_consume_regs(AyZ80* z, uint8_t* regs_out) {
    for (int i = 0; i < 14; ++i) regs_out[i] = z->ay_regs[i];
    if (!z->r13_fresh) regs_out[13] = 0xFF;
    z->r13_fresh = 0;
}

void ay_z80_get_diag(const AyZ80* z, AyZ80Diag* out) {
    out->out_writes = z->diag_out_writes;
    out->r13_writes = z->diag_r13_writes;
    out->int_acks   = z->diag_int_acks;
}

void ay_z80_reset_diag(AyZ80* z) {
    z->diag_out_writes = 0;
    z->diag_r13_writes = 0;
    z->diag_int_acks   = 0;
}
