/*
 * lh5.c -- single-file LH5 (-lh5-) decompressor
 *
 * Derived from lhasa by Simon Howard (https://github.com/fragglet/lhasa)
 * Original Copyright (c) 2011-2025, Simon Howard
 * ISC License (see LICENSE)
 *
 * The algorithm (tree_decode, bit_stream_reader, lh_new_decoder) is taken
 * verbatim from the lhasa source and re-packaged here as a standalone
 * buffer-to-buffer decompressor.  No functional changes to the core
 * algorithm; only the I/O layer (callback -> buffer cursor) is new.
 *
 * Adaptation Copyright (c) 2026, Playdate AY engine contributors
 * Same ISC License terms apply.
 */

#include "lh5.h"

#include <string.h>
#include <stdlib.h>

/* --------------------------------------------------------------------------
 * I/O adapter: wraps a (src, src_len) byte slice as the callback lhasa
 * expects.  lhasa calls back asking for N bytes; we hand them out of the
 * buffer until exhausted.
 * --------------------------------------------------------------------------*/

typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         pos;
} BufReader;

static size_t buf_reader_cb(void *out_buf, size_t n, void *ctx)
{
    BufReader *r = (BufReader *)ctx;
    size_t avail = r->len - r->pos;
    size_t give  = (n < avail) ? n : avail;
    if (give == 0) return 0;
    memcpy(out_buf, r->buf + r->pos, give);
    r->pos += give;
    return give;
}

/* --------------------------------------------------------------------------
 * Bit-stream reader  (verbatim from lhasa/lib/bit_stream_reader.c)
 * --------------------------------------------------------------------------*/

typedef size_t (*LHADecoderCallback)(void *buf, size_t buf_len, void *user);

typedef struct {
    LHADecoderCallback callback;
    void              *callback_data;
    uint32_t           bit_buffer;
    unsigned int       bits;
} BitStreamReader;

static void bit_stream_reader_init(BitStreamReader *reader,
                                   LHADecoderCallback callback,
                                   void *callback_data)
{
    reader->callback      = callback;
    reader->callback_data = callback_data;
    reader->bits          = 0;
    reader->bit_buffer    = 0;
}

static int peek_bits(BitStreamReader *reader, unsigned int n)
{
    uint8_t buf[4];
    size_t  bytes, i;

    if (n == 0) return 0;

    while (reader->bits < n) {
        const unsigned int fill_bytes = (32 - reader->bits) / 8;
        bytes = reader->callback(buf, fill_bytes, reader->callback_data);
        if (bytes == 0) return -1;
        for (i = 0; i < bytes; i++) {
            reader->bit_buffer |= (uint32_t)buf[i] << (24 - reader->bits);
            reader->bits += 8;
        }
    }
    return (int)(reader->bit_buffer >> (32 - n));
}

static int read_bits(BitStreamReader *reader, unsigned int n)
{
    int result = peek_bits(reader, n);
    if (result >= 0) {
        reader->bit_buffer <<= n;
        reader->bits -= n;
    }
    return result;
}

static int read_bit(BitStreamReader *reader)
{
    return read_bits(reader, 1);
}

/* --------------------------------------------------------------------------
 * Tree decoder  (verbatim from lhasa/lib/tree_decode.c)
 * --------------------------------------------------------------------------*/

typedef uint16_t TreeElement;

#define TREE_NODE_LEAF ((TreeElement)(1u << (sizeof(TreeElement) * 8 - 1)))

typedef struct {
    TreeElement *tree;
    unsigned int tree_len;
    unsigned int tree_allocated;
    unsigned int next_entry;
} TreeBuildData;

static void init_tree(TreeElement *tree, size_t tree_len)
{
    unsigned int i;
    for (i = 0; i < tree_len; ++i) tree[i] = TREE_NODE_LEAF;
}

static void set_tree_single(TreeElement *tree, TreeElement code)
{
    tree[0] = (TreeElement)(code | TREE_NODE_LEAF);
}

static void expand_queue(TreeBuildData *build)
{
    unsigned int end_offset;
    unsigned int new_nodes;

    new_nodes = (build->tree_allocated - build->next_entry) * 2;
    if (build->tree_allocated + new_nodes > build->tree_len) return;

    end_offset = build->tree_allocated;
    while (build->next_entry < end_offset) {
        build->tree[build->next_entry] = build->tree_allocated;
        build->tree_allocated += 2;
        ++build->next_entry;
    }
}

static unsigned int read_next_entry(TreeBuildData *build)
{
    unsigned int result;
    if (build->next_entry >= build->tree_allocated) return 0;
    result = build->next_entry;
    ++build->next_entry;
    return result;
}

static int add_codes_with_length(TreeBuildData *build,
                                 uint8_t *code_lengths,
                                 unsigned int num_code_lengths,
                                 unsigned int code_len)
{
    unsigned int i, node;
    int codes_remaining = 0;

    for (i = 0; i < num_code_lengths; ++i) {
        if (code_lengths[i] == code_len) {
            node = read_next_entry(build);
            build->tree[node] = (TreeElement)(i | TREE_NODE_LEAF);
        } else if (code_lengths[i] > code_len) {
            codes_remaining = 1;
        }
    }
    return codes_remaining;
}

static void build_tree(TreeElement *tree, size_t tree_len,
                       uint8_t *code_lengths, unsigned int num_code_lengths)
{
    TreeBuildData build;
    unsigned int  code_len;

    build.tree          = tree;
    build.tree_len      = tree_len;
    build.next_entry    = 0;
    build.tree_allocated = 1;

    code_len = 0;
    do {
        expand_queue(&build);
        ++code_len;
    } while (add_codes_with_length(&build, code_lengths,
                                   num_code_lengths, code_len));
}

static int read_from_tree(BitStreamReader *reader, TreeElement *tree)
{
    TreeElement code;
    int         bit;

    code = tree[0];
    while ((code & TREE_NODE_LEAF) == 0) {
        bit = read_bit(reader);
        if (bit < 0) return -1;
        code = tree[code + (unsigned int)bit];
    }
    return (int)(code & ~TREE_NODE_LEAF);
}

/* --------------------------------------------------------------------------
 * LH5 decoder state  (verbatim from lhasa/lib/lh_new_decoder.c / lh5_decoder.c)
 * --------------------------------------------------------------------------*/

#define HISTORY_BITS        14          /* 16 KiB ring buffer */
#define OFFSET_BITS         4
#define NUM_CODES           510
#define COPY_THRESHOLD      3
#define RING_BUFFER_SIZE    (1 << HISTORY_BITS)
/* Maximum bytes produced per lha_lh_new_read call. Upstream uses 16 KB for
 * streaming throughput; in our buffer-to-buffer mode the actual upper bound is
 * one max copy-from-history operation = NUM_CODES - 256 + COPY_THRESHOLD = 257
 * bytes. 512 is conservative. Reduces stack frame of lh5_decompress by ~16 KB. */
#define OUTPUT_BUFFER_SIZE  512
#define TEMP_CODE_BITS      5
#define MAX_TEMP_CODES      ((1 << TEMP_CODE_BITS) - 1)
#define MAX_OFFSET_CODES    ((1 << OFFSET_BITS) - 1)

typedef struct {
    BitStreamReader bit_stream_reader;
    uint8_t         ringbuf[RING_BUFFER_SIZE];
    unsigned int    ringbuf_pos;
    unsigned int    block_remaining;
    TreeElement     temp_tree[MAX_TEMP_CODES * 2];
    TreeElement     code_tree[NUM_CODES * 2];
    TreeElement     offset_tree[MAX_OFFSET_CODES * 2];
} LHANewDecoder;

static void init_ring_buffer(LHANewDecoder *decoder)
{
    memset(decoder->ringbuf, ' ', RING_BUFFER_SIZE);
    decoder->ringbuf_pos = 0;
}

static void lha_lh_new_init(LHANewDecoder *decoder,
                             LHADecoderCallback callback,
                             void *callback_data)
{
    bit_stream_reader_init(&decoder->bit_stream_reader, callback, callback_data);
    init_ring_buffer(decoder);
    decoder->block_remaining = 0;
    init_tree(decoder->code_tree,   NUM_CODES * 2);
    init_tree(decoder->offset_tree, MAX_OFFSET_CODES * 2);
    init_tree(decoder->temp_tree,   MAX_TEMP_CODES * 2);
}

static int read_length_value(LHANewDecoder *decoder)
{
    int i, len;
    len = read_bits(&decoder->bit_stream_reader, 3);
    if (len < 0) return -1;
    if (len == 7) {
        for (;;) {
            i = read_bit(&decoder->bit_stream_reader);
            if (i < 0) return -1;
            if (i == 0) break;
            ++len;
        }
    }
    return len;
}

static int read_temp_table(LHANewDecoder *decoder)
{
    int     i, j, n, len, code;
    uint8_t code_lengths[MAX_TEMP_CODES];

    n = read_bits(&decoder->bit_stream_reader, TEMP_CODE_BITS);
    if (n < 0) return 0;

    if (n == 0) {
        code = read_bits(&decoder->bit_stream_reader, 5);
        if (code < 0) return 0;
        set_tree_single(decoder->temp_tree, (TreeElement)code);
        return 1;
    }
    if (n > MAX_TEMP_CODES) n = MAX_TEMP_CODES;

    for (i = 0; i < n; ++i) {
        len = read_length_value(decoder);
        if (len < 0) return 0;
        code_lengths[i] = (uint8_t)len;
        if (i == 2) {
            len = read_bits(&decoder->bit_stream_reader, 2);
            if (len < 0) return 0;
            for (j = 0; j < len; ++j) {
                ++i;
                code_lengths[i] = 0;
            }
        }
    }
    build_tree(decoder->temp_tree, MAX_TEMP_CODES * 2, code_lengths, (unsigned int)n);
    return 1;
}

static int read_skip_count(LHANewDecoder *decoder, int skiprange)
{
    int result;
    if (skiprange == 0) {
        result = 1;
    } else if (skiprange == 1) {
        result = read_bits(&decoder->bit_stream_reader, 4);
        if (result < 0) return -1;
        result += 3;
    } else {
        result = read_bits(&decoder->bit_stream_reader, 9);
        if (result < 0) return -1;
        result += 20;
    }
    return result;
}

static int read_code_table(LHANewDecoder *decoder)
{
    int     i, j, n, skip_count, code;
    uint8_t code_lengths[NUM_CODES];

    n = read_bits(&decoder->bit_stream_reader, 9);
    if (n < 0) return 0;

    if (n == 0) {
        code = read_bits(&decoder->bit_stream_reader, 9);
        if (code < 0) return 0;
        set_tree_single(decoder->code_tree, (TreeElement)code);
        return 1;
    }
    if (n > NUM_CODES) n = NUM_CODES;

    i = 0;
    while (i < n) {
        code = read_from_tree(&decoder->bit_stream_reader, decoder->temp_tree);
        if (code < 0) return 0;
        if (code <= 2) {
            skip_count = read_skip_count(decoder, code);
            if (skip_count < 0) return 0;
            for (j = 0; j < skip_count && i < n; ++j) {
                code_lengths[i] = 0;
                ++i;
            }
        } else {
            code_lengths[i] = (uint8_t)(code - 2);
            ++i;
        }
    }
    build_tree(decoder->code_tree, NUM_CODES * 2, code_lengths, (unsigned int)n);
    return 1;
}

static int read_offset_table(LHANewDecoder *decoder)
{
    int     i, n, len, code;
    uint8_t code_lengths[MAX_OFFSET_CODES];

    n = read_bits(&decoder->bit_stream_reader, OFFSET_BITS);
    if (n < 0) return 0;

    if (n == 0) {
        code = read_bits(&decoder->bit_stream_reader, OFFSET_BITS);
        if (code < 0) return 0;
        set_tree_single(decoder->offset_tree, (TreeElement)code);
        return 1;
    }
    if (n > MAX_OFFSET_CODES) n = MAX_OFFSET_CODES;

    for (i = 0; i < n; ++i) {
        len = read_length_value(decoder);
        if (len < 0) return 0;
        code_lengths[i] = (uint8_t)len;
    }
    build_tree(decoder->offset_tree, MAX_OFFSET_CODES * 2, code_lengths, (unsigned int)n);
    return 1;
}

static int start_new_block(LHANewDecoder *decoder)
{
    int len = read_bits(&decoder->bit_stream_reader, 16);
    if (len < 0) return 0;
    decoder->block_remaining = (unsigned int)len;
    if (!read_temp_table(decoder))   return 0;
    if (!read_code_table(decoder))   return 0;
    if (!read_offset_table(decoder)) return 0;
    return 1;
}

static void output_byte(LHANewDecoder *decoder,
                         uint8_t *buf, size_t *buf_len, uint8_t b)
{
    buf[(*buf_len)++] = b;
    decoder->ringbuf[decoder->ringbuf_pos] = b;
    decoder->ringbuf_pos = (decoder->ringbuf_pos + 1) % RING_BUFFER_SIZE;
}

static int read_offset_code(LHANewDecoder *decoder)
{
    int bits = read_from_tree(&decoder->bit_stream_reader, decoder->offset_tree);
    if (bits < 0) return -1;
    if (bits == 0) return 0;
    if (bits == 1) return 1;
    {
        int result = read_bits(&decoder->bit_stream_reader, bits - 1);
        if (result < 0) return -1;
        return result + (1 << (bits - 1));
    }
}

static void copy_from_history(LHANewDecoder *decoder,
                               uint8_t *buf, size_t *buf_len, size_t count)
{
    int          offset;
    unsigned int i, start;

    offset = read_offset_code(decoder);
    if (offset < 0) return;

    start = decoder->ringbuf_pos + RING_BUFFER_SIZE - (unsigned int)offset - 1;
    for (i = 0; i < count; ++i) {
        output_byte(decoder, buf, buf_len,
                    decoder->ringbuf[(start + i) % RING_BUFFER_SIZE]);
    }
}

/* One "read" step: decode one command, writing up to OUTPUT_BUFFER_SIZE bytes
 * into buf.  Returns number of bytes written, or 0 on end/error. */
static size_t lha_lh_new_read(LHANewDecoder *decoder, uint8_t *buf)
{
    size_t result = 0;
    int    code, copy_count;

    while (decoder->block_remaining == 0) {
        if (!start_new_block(decoder)) return 0;
    }
    --decoder->block_remaining;

    code = read_from_tree(&decoder->bit_stream_reader, decoder->code_tree);
    if (code < 0) return 0;

    if (code < 256) {
        output_byte(decoder, buf, &result, (uint8_t)code);
    } else {
        copy_count = code - 256 + COPY_THRESHOLD;
        copy_from_history(decoder, buf, &result, (size_t)copy_count);
    }
    return result;
}

/* --------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------*/

int lh5_decompress(const uint8_t *src, size_t src_len,
                   uint8_t *dst, size_t dst_len)
{
    /* LHANewDecoder is ~18 KB (16 KB ring buffer + 2 KB Huffman trees).
     * Putting it on the stack overflows the Playdate's 60 KB gameTask
     * stack once Lua + l_load + vtx_open frames are also accounted for
     * (verified by an on-device "stack overflow in task gameTask" crash
     * when loading the VTX tune). lh5_decompress is only ever called from
     * the Lua/main thread (engine_load → {vtx,ym}_open), so a single
     * static instance is safe and avoids the malloc on a small-heap target.
     */
    static LHANewDecoder decoder;
    BufReader            reader;
    uint8_t              outbuf[OUTPUT_BUFFER_SIZE];
    size_t               written = 0;
    size_t               n;

    if (!src || !dst || dst_len == 0) return -1;

    reader.buf = src;
    reader.len = src_len;
    reader.pos = 0;

    lha_lh_new_init(&decoder, buf_reader_cb, &reader);

    while (written < dst_len) {
        n = lha_lh_new_read(&decoder, outbuf);
        if (n == 0) break;
        if (n > dst_len - written) n = dst_len - written;
        memcpy(dst + written, outbuf, n);
        written += n;
    }

    return (int)written;
}
