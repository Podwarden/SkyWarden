#include "formats/vtx.h"
#include "lh5.h"
#include <stdlib.h>
#include <string.h>

/*
 * VTX open: parse the real-world VTX format used by all known players.
 *
 * Fixed header (16 bytes):
 *   [0-1]   magic: 'ay' or 'ym'
 *   [2]     stereo mode
 *   [3-4]   loop_frame LE16
 *   [5-8]   chip_clock_hz LE32
 *   [9]     frame_rate_hz
 *   [10-11] year LE16
 *   [12-15] regdata_size LE32 (= frame_count * 14)
 *
 * Followed by 5 null-terminated strings (title, author, from, tracker, comment),
 * then the raw LH5-compressed column-major register data.
 */
int vtx_open(VtxFile* vtx, const uint8_t* buf, size_t len)
{
    /* Free any previously-decompressed frame buffer before zeroing the struct.
     * Callers must either zero-initialise VtxFile before the first open, or
     * have gone through vtx_close (which sets frames=NULL).  Saving the
     * pointer first lets us memset cleanly and then release. */
    uint8_t* old_frames = vtx->frames;
    memset(vtx, 0, sizeof(*vtx));
    free(old_frames);   /* free(NULL) is a safe no-op on first open */
    if (len < 16) return 0;

    /* Two-byte magic: 'ay' = AY-3-8910, 'ym' = YM2149 */
    if      (buf[0] == 'a' && buf[1] == 'y') vtx->chip_is_ym = 0;
    else if (buf[0] == 'y' && buf[1] == 'm') vtx->chip_is_ym = 1;
    else return 0;

    vtx->stereo        = buf[2];
    vtx->loop_frame    = (uint16_t)(buf[3] | ((uint16_t)buf[4] << 8));
    vtx->chip_clock_hz = (uint32_t)(buf[5] | ((uint32_t)buf[6] << 8)
                                  | ((uint32_t)buf[7] << 16) | ((uint32_t)buf[8] << 24));
    vtx->frame_rate_hz = (buf[9] != 0) ? buf[9] : 50; /* treat 0 as default 50 Hz */
    vtx->year          = (uint16_t)(buf[10] | ((uint16_t)buf[11] << 8));
    uint32_t regdata_size = (uint32_t)(buf[12] | ((uint32_t)buf[13] << 8)
                                     | ((uint32_t)buf[14] << 16) | ((uint32_t)buf[15] << 24));

    if (regdata_size == 0 || regdata_size % 14 != 0) return 0;
    /* Sanity cap: real-world VTX tunes are under ~200KB uncompressed.
     * Reject pathological values to (a) protect malloc on small heap and
     * (b) avoid (int)regdata_size UB in the int comparison below. */
    if (regdata_size > 1024 * 1024) return 0;

    /* Skip 5 null-terminated strings starting at offset 16 */
    size_t off = 16;
    for (int s = 0; s < 5; ++s) {
        while (off < len && buf[off] != 0) off++;
        if (off >= len) return 0;
        off++; /* consume the null terminator */
    }
    if (off >= len) return 0;

    size_t payload_len = len - off;

    /* Decompress LH5 payload into a heap buffer (column-major layout) */
    uint8_t* col_major = (uint8_t*)malloc(regdata_size);
    if (!col_major) return 0;
    int n = lh5_decompress(buf + off, payload_len, col_major, regdata_size);
    if (n != (int)regdata_size) { free(col_major); return 0; }

    vtx->frame_count = regdata_size / 14;

    /*
     * Transpose column-major -> row-major so vtx_next_frame can memcpy 14 bytes.
     * Column-major: col_major[r * frame_count + f] = register r at frame f
     * Row-major:    row_major[f * 14 + r]           = register r at frame f
     */
    uint8_t* row_major = (uint8_t*)malloc(regdata_size);
    if (!row_major) { free(col_major); return 0; }
    for (uint32_t r = 0; r < 14; ++r) {
        for (uint32_t f = 0; f < vtx->frame_count; ++f) {
            row_major[(size_t)f * 14 + r] = col_major[(size_t)r * vtx->frame_count + f];
        }
    }
    free(col_major);
    vtx->frames = row_major;
    vtx->cursor = 0;
    return 1;
}

void vtx_close(VtxFile* vtx)
{
    free(vtx->frames);
    vtx->frames = NULL;
    vtx->frame_count = 0;
    vtx->cursor = 0;
}

int vtx_next_frame(VtxFile* vtx, uint8_t* regs)
{
    if (!vtx->frames || vtx->cursor >= vtx->frame_count) return 0;
    memcpy(regs, vtx->frames + (size_t)vtx->cursor * 14, 14);
    vtx->cursor++;
    return 1;
}

/* --------------------------------------------------------------------------
 * Vtable adapter
 * -------------------------------------------------------------------------- */

static int vtx_open_vt(void* state, const uint8_t* buf, size_t len, int* fr_out)
{
    VtxFile* v = (VtxFile*)state;
    if (!vtx_open(v, buf, len)) return 0;
    *fr_out = v->frame_rate_hz;
    return 1;
}

static int vtx_next_frame_vt(void* state, uint8_t* regs)
{
    return vtx_next_frame((VtxFile*)state, regs);
}

static void vtx_close_vt(void* state)
{
    vtx_close((VtxFile*)state);
}

const FormatVTable vtx_vtable = {
    .open       = vtx_open_vt,
    .next_frame = vtx_next_frame_vt,
    .close      = vtx_close_vt,
    .set_position = NULL,        /* VTX has no order list to seek */
    .state_size = sizeof(VtxFile),
};
