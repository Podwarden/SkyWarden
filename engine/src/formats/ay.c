#include "formats/ay.h"
#include "z80emu.h"     /* Z80_STATE / Z80_A for the song-index → register-A seed */
#include <stdlib.h>
#include <string.h>

#define AY_Z80_CLOCK_HZ 3500000   /* nominal ZX Spectrum 48K Z80 clock */

/* Read a big-endian WORD from buffer position `at`. Returns 0 if out of bounds. */
static uint16_t rd_be16_at(const uint8_t* buf, size_t len, size_t at) {
    if (at + 2 > len) return 0;
    return (uint16_t)((buf[at] << 8) | buf[at + 1]);
}

/* Read a big-endian WORD as a SIGNED relative offset (for the .ay format's
 * "relative to own position" pointers). Returns 0 if out of bounds. */
static int16_t rd_rel16_at(const uint8_t* buf, size_t len, size_t at) {
    if (at + 2 > len) return 0;
    return (int16_t)((buf[at] << 8) | buf[at + 1]);
}

int ay_open(AyFile* a, const uint8_t* buf, size_t len, int song_index) {
    memset(a, 0, sizeof(*a));

    /* Sanity bound: .ay files are typically 1-100 KB. */
    if (len < 20 || len > 1024 * 1024) return 0;

    /* Magic check: "ZXAYEMUL" — we don't support AYBR/etc. */
    if (memcmp(buf, "ZXAY", 4) != 0) return 0;
    if (memcmp(buf + 4, "EMUL", 4) != 0) return 0;

    /* off 16: num_songs - 1, off 17: first_song. */
    uint8_t num_songs  = (uint8_t)(buf[16] + 1);
    uint8_t first_song = buf[17];
    a->num_songs = num_songs;

    /* Resolve song index: caller-explicit, fall back to file default. */
    int sidx = (song_index >= 0) ? song_index : (int)first_song;
    if (sidx < 0 || sidx >= (int)num_songs) return 0;
    a->current_song = (uint8_t)sidx;

    /* off 18: songs_struct_offset (relative to position 18). */
    int16_t songs_rel = rd_rel16_at(buf, len, 18);
    size_t songs_pos = (size_t)((intptr_t)18 + songs_rel);
    if (songs_pos + (size_t)sidx * 4 + 4 > len) return 0;
    a->songs_pos = songs_pos;

    /* Per-song descriptor: 2 WORDs (name_offset, song_data_offset). */
    size_t song_desc_pos = songs_pos + (size_t)sidx * 4;
    int16_t data_rel = rd_rel16_at(buf, len, song_desc_pos + 2);
    size_t song_data_pos = (size_t)((intptr_t)(song_desc_pos + 2) + data_rel);
    if (song_data_pos + 12 > len) return 0;

    /* Per-song data record layout (per Project AY spec / vgmrips wiki):
     *   +0..+3  AChan/BChan/CChan/Noise (Amiga channel mapping; not used here)
     *   +4..+5  SongLength (frames)
     *   +6..+7  FadeLength
     *   +8      HiReg (initial R6/R12 value)
     *   +9      LoReg (initial R1/R7 value)
     *   +10..+11 PPoints  — signed BE16 rel offset to a separate {SP, Init, Inter} record
     *   +12..+13 PAddresses — signed BE16 rel offset to first block record
     *
     * Earlier code mistakenly read SP/INIT/INT directly from +6/+8/+10. That
     * happened to land on FadeLength/HiReg+LoReg/low-byte-of-PPoints, producing
     * bogus addresses that occasionally NOP-slid into real player code
     * (explaining why some tunes "almost played" before collapsing). */
    int16_t ppoints_rel = rd_rel16_at(buf, len, song_data_pos + 10);
    size_t  ppoints_pos = (size_t)((intptr_t)(song_data_pos + 10) + ppoints_rel);
    if (ppoints_pos + 6 > len) return 0;
    a->stack_pointer  = rd_be16_at(buf, len, ppoints_pos + 0);
    a->init_addr      = rd_be16_at(buf, len, ppoints_pos + 2);
    a->interrupt_addr = rd_be16_at(buf, len, ppoints_pos + 4);
    /* Sample-track detection: Inter==0x001B (Spectrum GET-CHAR ROM vector) is
     * the Project AY convention sentinel meaning "synchronous CPU-driven DAC
     * player — do not interrupt me". Used by Robocop subsongs 5-8 etc.
     * Drives a different stepping model (no INT, no block-call INIT). */
    a->is_sample_track = (a->interrupt_addr == 0x001B) ? 1 : 0;
    if (a->interrupt_addr == 0) a->interrupt_addr = 0x0038;

    /* HiReg / LoReg per-song selectors. These are pre-loaded into AY registers
     * before INIT runs — some tunes (like Star Pilot) encode subsong selection
     * purely via these initial register values (songs share INIT/INT addresses
     * but differ in HiReg). Per spec: HiReg sets R6 + R12, LoReg sets R1 + R7. */
    a->hi_reg = buf[song_data_pos + 8];
    a->lo_reg = buf[song_data_pos + 9];

    /* Per Project AY spec, INIT=0 means "no separate init — use song 0's
     * init". Multi-song .ay tunes typically only put a real INIT addr on
     * song 0; subsequent songs share the player code and rely on the same
     * initialisation. Fall back to song 0's INIT addr when sidx > 0 and
     * the selected song's INIT is 0. Note: song 0's INIT lives behind the
     * same PPoints indirection — chase the rel pointer at +10 of song 0's
     * SongData record, then read the Init WORD at offset +2 of the Points
     * record. */
    if (a->init_addr == 0 && sidx != 0) {
        size_t s0_desc = songs_pos;            /* song 0 desc at offset 0 in songs array */
        if (s0_desc + 4 <= len) {
            int16_t s0_rel = rd_rel16_at(buf, len, s0_desc + 2);
            size_t  s0_pos = (size_t)((intptr_t)(s0_desc + 2) + s0_rel);
            if (s0_pos + 12 <= len) {
                int16_t s0_pp_rel = rd_rel16_at(buf, len, s0_pos + 10);
                size_t  s0_pp_pos = (size_t)((intptr_t)(s0_pos + 10) + s0_pp_rel);
                if (s0_pp_pos + 4 <= len) {
                    a->init_addr = rd_be16_at(buf, len, s0_pp_pos + 2);
                }
            }
        }
    }

    /* Some .ay files set SP=0; that means "use whatever's reasonable".
     * Default to high RAM (0xFFFF) so pushes don't clobber loaded code. */
    if (a->stack_pointer == 0) a->stack_pointer = 0xFFFF;

    int16_t blocks_rel = rd_rel16_at(buf, len, song_data_pos + 12);
    size_t blocks_pos = (size_t)((intptr_t)(song_data_pos + 12) + blocks_rel);

    if (!ay_z80_init(&a->z80)) return 0;

    /* Walk the blocks list, loading each into Z80 RAM at its declared address. */
    size_t bp = blocks_pos;
    int max_blocks = 256;   /* sanity: don't loop forever on malformed files */
    while (bp + 6 <= len && max_blocks-- > 0) {
        uint16_t load_addr  = rd_be16_at(buf, len, bp);
        if (load_addr == 0) break;     /* sentinel: end of block list */
        uint16_t block_len  = rd_be16_at(buf, len, bp + 2);
        int16_t  data_rel2  = rd_rel16_at(buf, len, bp + 4);
        size_t   data_at    = (size_t)((intptr_t)(bp + 4) + data_rel2);
        if (block_len == 0 || data_at + block_len > len) {
            ay_z80_close(&a->z80);
            return 0;
        }
        ay_z80_load(&a->z80, load_addr, buf + data_at, (size_t)block_len);
        bp += 6;
    }

    /* Install RST 0x38 stub: JP interrupt_addr.
     * Opcode 0xC3 = JP nn (3 bytes total: opcode + low + high). */
    uint8_t rst_stub[3] = {
        0xC3,
        (uint8_t)(a->interrupt_addr & 0xFF),
        (uint8_t)(a->interrupt_addr >> 8),
    };
    ay_z80_load(&a->z80, 0x0038, rst_stub, 3);

    /* Pre-load the per-song HiReg / LoReg AY register seeds BEFORE running
     * INIT. The .ay spec defines HiReg as the initial value of R6 (noise
     * period) AND R12 (envelope period MSB); LoReg as R1 (tone A coarse)
     * AND R7 (mixer). Star Pilot and other multi-song tunes encode subsong
     * selection entirely via these seeds — the player reads them through
     * `IN A,(0xFFFD)` during INIT to decide which song to play. */
    a->z80.ay_regs[1]  = a->lo_reg;
    a->z80.ay_regs[6]  = a->hi_reg;
    a->z80.ay_regs[7]  = a->lo_reg;
    a->z80.ay_regs[12] = a->hi_reg;

    /* Pre-load Z80 register A with the song index (0..num_songs-1). Some
     * .ay players (Robocop and others by Jonathan Dunn) read A inside INIT
     * as the song selector — e.g. Robocop's $C008 does `LD ($00FF),A`. */
    if (a->z80.cpu) {
        ((Z80_STATE*)a->z80.cpu)->registers.byte[Z80_A] = (uint8_t)sidx;
    }

    /* Some .ay files also call HALT in their interrupt routine, which means
     * the CPU sits halted until the next INT. To keep that working, we DON'T
     * need any RAM at the HALT spot — just letting it loop on HALT until our
     * next ay_z80_interrupt_frame is fine. */

    /* Call INIT — unless this is a sample-track player, in which case INIT is
     * the synchronous player's main routine that plays the whole sample inline
     * and RETs. We don't want to block here for several seconds + miss the
     * in-progress AY register changes; instead seed PC/SP and let per-sample
     * stepping advance the Z80 in lockstep with the audio output rate.
     *
     * For regular chiptune players, INIT runs to RET / EI;HALT as expected,
     * the trampoline at 0x0000 catches RET-style returns, and per-sample
     * stepping drives 50 Hz INTs into the player's main loop. */
    if (a->is_sample_track && a->init_addr != 0) {
        Z80_STATE* st = (Z80_STATE*)a->z80.cpu;
        st->pc = a->init_addr;
        st->registers.word[Z80_SP] = a->stack_pointer;
        a->z80.halted = 0;
    } else if (a->init_addr != 0) {
        /* Per Project AY format spec, INIT=0 means "no separate init". We
         * already fell back to song 0's INIT above when applicable; if it's
         * still 0, skip the call entirely to avoid jumping into our sentinel
         * HALT byte at 0x0000. */
        ay_z80_call(&a->z80, a->init_addr, a->stack_pointer, 2000000);
    }

    /* Per-sample stepping: report frame_rate_hz = sample_rate so the engine
     * calls next_frame every audio sample, and we run only ~79 Z80 T-states
     * per call (trivially under per-sample budget). Every (sample_rate / 50)
     * samples we assert INT to drive the player's 50 Hz interrupt service. */
    a->sample_rate          = 44100;
    a->ticks_per_int_frame  = AY_Z80_CLOCK_HZ / 50;                /* 70000 */
    a->ticks_per_sample     = AY_Z80_CLOCK_HZ / a->sample_rate;    /* ~79 */
    a->ticks_until_next_int = 0;                                   /* fire INT on first call */
    a->frame_rate_hz        = (uint32_t)a->sample_rate;            /* triggers per-sample callback */

    /* Stash the buffer for engine_ay_set_song to restart from. Owned by caller. */
    a->tune_buf = buf;
    a->tune_len = len;
    a->loaded = 1;
    return 1;
}

int ay_song_count(const AyFile* a)   { return (int)a->num_songs; }
int ay_current_song(const AyFile* a) { return (int)a->current_song; }

int ay_next_frame(AyFile* a, uint8_t* regs) {
    if (!a->loaded) return 0;

    /* Spread Z80 work per audio sample. Each call: do ~79 Z80 T-states.
     * Sample-track players (Inter==0x001B) are synchronous DAC bit-bangers
     * that don't want our 50 Hz INT — it'd derail the sample timing. They
     * just need pure stepping; INIT is their whole player, and per-sample
     * stepping advances it in lockstep with the audio output. */
    if (a->is_sample_track) {
        ay_z80_step(&a->z80, a->ticks_per_sample);
    } else if (a->ticks_until_next_int <= 0) {
        ay_z80_assert_int_then_step(&a->z80, a->ticks_per_sample);
        a->ticks_until_next_int = a->ticks_per_int_frame;
    } else {
        ay_z80_step(&a->z80, a->ticks_per_sample);
    }
    a->ticks_until_next_int -= a->ticks_per_sample;

    /* Copy AY register snapshot (R0..R13 only — the engine only cares about those).
     * Use consume_regs so R13 appears as 0xFF (no envelope change) unless the Z80
     * actually wrote it since the last call — prevents envelope-shape retrigger spam. */
    ay_z80_consume_regs(&a->z80, regs);
    return 1;
}

void ay_close(AyFile* a) {
    if (a->loaded) {
        ay_z80_close(&a->z80);
        a->loaded = 0;
    }
    memset(a, 0, sizeof(*a));
}

/* Vtable adapter ===================================================== */

static int ay_open_vt(void* state, const uint8_t* buf, size_t len, int* fr_out) {
    AyFile* a = (AyFile*)state;
    /* The format vtable doesn't know about song_index — use the file's
     * declared first_song. Per-song selection happens via engine_ay_set_song. */
    if (!ay_open(a, buf, len, /*song_index*/ -1)) return 0;
    *fr_out = (int)a->frame_rate_hz;
    return 1;
}
static int ay_next_frame_vt(void* state, uint8_t* regs) {
    return ay_next_frame((AyFile*)state, regs);
}
static void ay_close_vt(void* state) {
    ay_close((AyFile*)state);
}

const FormatVTable ay_vtable = {
    .open       = ay_open_vt,
    .next_frame = ay_next_frame_vt,
    .close      = ay_close_vt,
    .set_position = NULL,        /* .ay seeks via engine_ay_set_song, not order list */
    .state_size = sizeof(AyFile),
};
