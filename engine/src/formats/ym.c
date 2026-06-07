#include "formats/ym.h"
#include "lh5.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rd_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
static uint16_t rd_be16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

/* LHA "level 0" header layout (the only one we need to handle):
 *   off 0:  uint8   header_size (excluding this byte and the next checksum byte)
 *   off 1:  uint8   header_checksum (we ignore for trust)
 *   off 2:  char[5] method ("-lh5-" for LH5 compression — only one we support)
 *   off 7:  uint32  packed_size (little-endian)
 *   off 11: uint32  unpacked_size (little-endian)
 *   off 15: uint32  timestamp (ignored)
 *   off 19: uint8   attribute
 *   off 20: uint8   level (0 / 1 / 2)
 *   off 21: uint8   filename_len
 *   off 22: char[]  filename
 *   off 22+filename_len: uint16 crc (ignored)
 *   then header tail (level 1+ has more)
 *   then packed_size bytes of LH5 payload starting at offset header_size + 2.
 *
 * Returns 1 on successful unwrap (out_buf/out_len populated, caller owns out_buf),
 * 0 if not an LHA file or unwrapping failed.
 */
static int try_unwrap_lha(const uint8_t* buf, size_t len,
                          uint8_t** out_buf, size_t* out_len) {
    if (len < 22) return 0;
    if (memcmp(buf + 2, "-lh5-", 5) != 0) return 0;
    uint8_t hdr_size = buf[0];
    if (hdr_size == 0) return 0;
    size_t payload_off = (size_t)hdr_size + 2;
    if (payload_off >= len) return 0;
    uint32_t packed_size = (uint32_t)buf[7]  | ((uint32_t)buf[8]  << 8) | ((uint32_t)buf[9]  << 16) | ((uint32_t)buf[10] << 24);
    uint32_t unpacked    = (uint32_t)buf[11] | ((uint32_t)buf[12] << 8) | ((uint32_t)buf[13] << 16) | ((uint32_t)buf[14] << 24);
    /* Subtract, don't add — payload_off < len was checked above, so len - payload_off is safe.
     * This avoids 32-bit overflow when packed_size is near UINT32_MAX. */
    if (packed_size > len - payload_off) return 0;
    /* Sanity cap matching the raw YM3 path. */
    if (unpacked == 0 || unpacked > 1024 * 1024) return 0;

    uint8_t* dst = (uint8_t*)malloc(unpacked);
    if (!dst) return 0;
    int n = lh5_decompress(buf + payload_off, packed_size, dst, unpacked);
    if (n != (int)unpacked) { free(dst); return 0; }

    *out_buf = dst;
    *out_len = unpacked;
    return 1;
}

/* YM5/YM6 layout after the 4-byte magic — see Plan 03 Task 1.
 * Returns 1 on success, 0 on bad/unsupported header (DigiDrum, wrong layout).
 *
 * Terminology note: YM5 "interleaved" (attributes bit0=1) means column-major
 * storage — all frames for register 0, then all frames for register 1, etc.
 * This matches our ym_next_frame which does regs[r] = streams[r*frame_count + cursor].
 * We reject bit0=0 (row-major / non-interleaved) since we cannot play it back. */
static int parse_ym5_or_ym6(YmFile* ym, const uint8_t* buf, size_t len) {
    if (len < 34) return 0;
    if (memcmp(buf + 4, "LeOnArD!", 8) != 0) return 0;

    uint32_t frame_count = rd_be32(buf + 12);
    uint32_t attributes  = rd_be32(buf + 16);
    uint16_t dd_count    = rd_be16(buf + 20);
    /* off 22..25 = master_clock, ignored */
    uint16_t player_rate = rd_be16(buf + 26);
    /* off 28..31 = loop_frame; we don't currently expose loop, so ignored */
    uint16_t addl_size   = rd_be16(buf + 32);

    /* Plan 03 doesn't support DigiDrum — refuse files that need it. */
    if (dd_count != 0) return 0;

    /* Sanity bounds. */
    if (frame_count == 0 || frame_count > 1024u * 1024u) return 0;
    if (player_rate < 25 || player_rate > 1000)          return 0;

    /* Bit 0 of attributes = 1 means column-major (YM5 spec calls this "interleaved"):
     * reg0[frame0..N], reg1[frame0..N], ... reg13[frame0..N].
     * Our ym_next_frame is column-major — accept bit0=1.
     * Reject bit0=0 (row-major / non-interleaved) — not supported in Plan 03. */
    if (!(attributes & 1)) return 0;

    /* Skip past additional data and 3 null-terminated strings. */
    size_t off = 34 + addl_size;
    for (int s = 0; s < 3; ++s) {
        while (off < len && buf[off] != 0) ++off;
        if (off >= len) return 0;
        ++off;
    }

    /* Need frame_count * 14 bytes of register data. */
    if (off + (size_t)frame_count * 14 > len) return 0;

    ym->frame_count   = frame_count;
    ym->frame_rate_hz = player_rate;
    ym->streams       = buf + off;
    ym->cursor        = 0;
    return 1;
}

int ym_open(YmFile* ym, const uint8_t* buf, size_t len) {
    /* Free any previously-owned decompressed buffer before zeroing. */
    uint8_t* old_owned = ym->owned_buf;
    memset(ym, 0, sizeof(*ym));
    free(old_owned);

    /* If the file is LHA-wrapped, decompress to a heap buffer and use that
     * as the actual YM data going forward. */
    uint8_t* unwrapped = NULL;
    size_t   unwrapped_len = 0;
    if (try_unwrap_lha(buf, len, &unwrapped, &unwrapped_len)) {
        ym->owned_buf = unwrapped;
        buf = unwrapped;
        len = unwrapped_len;
    }

    if (len < 4) { ym_close(ym); return 0; }

    if (memcmp(buf, "YM3!", 4) == 0) {
        ym->variant       = 3;
        ym->frame_rate_hz = 50;
        ym->streams       = buf + 4;
        size_t data_len   = len - 4;
        if (data_len == 0 || data_len % 14 != 0) { ym_close(ym); return 0; }
        /* Sanity cap matching vtx.c — real YM files are well under 1 MiB. */
        if (data_len > 1024 * 1024)              { ym_close(ym); return 0; }
        ym->frame_count   = (uint32_t)(data_len / 14);
        return 1;
    }
    if (memcmp(buf, "YM5!", 4) == 0 || memcmp(buf, "YM6!", 4) == 0) {
        ym->variant = (buf[2] == '5') ? 5 : 6;
        if (!parse_ym5_or_ym6(ym, buf, len)) {
            ym_close(ym);
            return 0;
        }
        return 1;
    }
    ym_close(ym);
    return 0;
}

void ym_close(YmFile* ym) {
    free(ym->owned_buf);
    ym->owned_buf = NULL;
    ym->streams = NULL;
    ym->frame_count = 0;
    ym->cursor = 0;
}

int ym_next_frame(YmFile* ym, uint8_t* regs) {
    if (!ym->streams || ym->cursor >= ym->frame_count) return 0;
    /* YM3 is column-major: stream r at offset r*frame_count, value at +cursor. */
    for (int r = 0; r < 14; ++r) {
        regs[r] = ym->streams[(size_t)r * ym->frame_count + ym->cursor];
    }
    ym->cursor++;
    return 1;
}

/* Vtable adapter */

static int ym_open_vt(void* state, const uint8_t* buf, size_t len, int* fr_out) {
    YmFile* y = (YmFile*)state;
    if (!ym_open(y, buf, len)) return 0;
    *fr_out = (int)y->frame_rate_hz;
    return 1;
}
static int ym_next_frame_vt(void* state, uint8_t* regs) {
    return ym_next_frame((YmFile*)state, regs);
}
static void ym_close_vt(void* state) {
    ym_close((YmFile*)state);
}

const FormatVTable ym_vtable = {
    .open       = ym_open_vt,
    .next_frame = ym_next_frame_vt,
    .close      = ym_close_vt,
    .set_position = NULL,        /* YM has no order list to seek */
    .state_size = sizeof(YmFile),
};
