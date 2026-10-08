#ifndef ENGINE_FORMATS_AY_H
#define ENGINE_FORMATS_AY_H

#include <stddef.h>
#include <stdint.h>
#include "format_vtable.h"
#include "ay_z80_glue.h"

typedef struct AyFile {
    AyZ80    z80;
    uint16_t init_addr;
    uint16_t interrupt_addr;
    uint16_t stack_pointer;
    uint32_t frame_rate_hz;          /* = sample_rate; engine calls next_frame per audio sample */
    uint8_t  loaded;
    uint8_t  num_songs;              /* 1..N, parsed from header */
    uint8_t  current_song;           /* 0..num_songs-1 */
    uint8_t  hi_reg;                 /* SongData+8 — initial R6/R12 value (per-song selector!) */
    uint8_t  lo_reg;                 /* SongData+9 — initial R1/R7 value */
    uint8_t  is_sample_track;        /* 1 if Inter==0x001B (synchronous DAC sample player) */
    uint8_t  _pad[2];                /* alignment */

    /* Per-audio-sample stepping accounting. We run ~79 Z80 ticks per audio
     * sample at 44.1 kHz / 3.5 MHz. Every (sample_rate / 50) samples we
     * assert INT to fire the player's interrupt service routine. */
    int      sample_rate;            /* hosted at, e.g., 44100 */
    int      ticks_per_sample;       /* ~79 = 3500000 / 44100 */
    int      ticks_until_next_int;   /* counts down; on hitting <=0 we assert INT */
    int      ticks_per_int_frame;    /* ~70000 = 3500000 / 50 */

    /* Hold the buffer for set_song restart. Owned by caller; we don't free. */
    const uint8_t* tune_buf;
    size_t   tune_len;

    /* Offset within tune_buf where the songs-struct array starts. Each entry
     * is 4 bytes (name_offset BE16, data_offset BE16). Cached during ay_open
     * so engine_ay_set_song can look up subsong N's data record without
     * re-parsing the whole header (and without disturbing Z80/RAM/chip
     * state). */
    size_t   songs_pos;
} AyFile;

/* song_index < 0 -> use the file's declared first_song. Otherwise selects
 * that 0-based index (0..num_songs-1). Returns 0 if out of range. */
int  ay_open(AyFile* a, const uint8_t* buf, size_t len, int song_index);
void ay_close(AyFile* a);
int  ay_next_frame(AyFile* a, uint8_t* regs);

int  ay_song_count(const AyFile* a);
int  ay_current_song(const AyFile* a);

extern const FormatVTable ay_vtable;

#endif
