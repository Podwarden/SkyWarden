#ifndef ENGINE_FORMATS_VTX_H
#define ENGINE_FORMATS_VTX_H

#include <stddef.h>
#include <stdint.h>
#include "format_vtable.h"

/*
 * VTX (Vortex) file format — real-world format used by all known players.
 *
 * Header layout (16 bytes fixed, then 5 null-terminated strings):
 *   [0-1]   magic: 'ay' (AY-3-8910) or 'ym' (YM2149)
 *   [2]     stereo mode (0-6)
 *   [3-4]   loop_frame LE16
 *   [5-8]   chip_clock_hz LE32 (1773400 for ZX Spectrum)
 *   [9]     frame_rate_hz (50 for ZX Spectrum)
 *   [10-11] year LE16
 *   [12-15] regdata_size LE32 (unpacked bytes = frame_count * 14)
 *   [16+]   5 null-terminated strings: title, author, from, tracker, comment
 *   then:   LH5-compressed register data (column-major layout)
 *
 * Reference: https://github.com/asashnov/libayemu/blob/master/contrib/vtx_format.txt
 *
 * Note: some documentation describes a newer format with 3-byte magic ('ay!'/'ym!')
 * and a different header. All files in the wild use the 2-byte magic format above.
 */

typedef struct {
    int            chip_is_ym;      /* 1 if YM2149 ('ym' magic), 0 if AY-3-8910 ('ay' magic) */
    int            stereo;
    uint16_t       loop_frame;
    uint32_t       chip_clock_hz;
    uint16_t       frame_rate_hz;
    uint16_t       year;
    /* Frame data: malloc'd row-major buffer, frame_count * 14 bytes. */
    uint8_t*       frames;
    uint32_t       frame_count;
    uint32_t       cursor;
} VtxFile;

int  vtx_open(VtxFile* vtx, const uint8_t* buf, size_t len);
void vtx_close(VtxFile* vtx);
int  vtx_next_frame(VtxFile* vtx, uint8_t* regs);

extern const FormatVTable vtx_vtable;

#endif
