/*
 * lh5.h -- single-file LH5 (-lh5-) decompressor
 *
 * Derived from lhasa by Simon Howard (https://github.com/fragglet/lhasa)
 * Original Copyright (c) 2011-2025, Simon Howard
 * ISC License (see LICENSE)
 *
 * Adaptation to a standalone buffer-to-buffer API:
 *   Copyright (c) 2026, Playdate AY engine contributors
 *   Same ISC License terms apply.
 *
 * LH5 is the LZSS-variant compression algorithm used by LHA v2+ and
 * the VTX / YM chiptune file formats.
 */

#ifndef LH5_H
#define LH5_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decompress LH5-compressed data.
 *
 * @param src       Pointer to the compressed data buffer.
 * @param src_len   Length of the compressed data in bytes.
 * @param dst       Pointer to the output (decompressed) buffer.
 * @param dst_len   Capacity of the output buffer in bytes; decompression
 *                  stops when this many bytes have been written.
 * @return          Number of bytes written to dst, or -1 on error.
 */
int lh5_decompress(const uint8_t *src, size_t src_len,
                   uint8_t *dst, size_t dst_len);

#ifdef __cplusplus
}
#endif

#endif /* LH5_H */
