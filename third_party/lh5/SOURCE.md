# lh5

Vendored from https://github.com/fragglet/lhasa at commit f4d9798838972ebfb34563e023532a9fb97f1344
License: ISC (equivalent to BSD-2-Clause / MIT; see LICENSE)
Author: Simon Howard

LH5 (LZSS-variant) decompressor as used by the LHA archive format,
the VTX chiptune format, and the YM/YM3-LH5 chiptune wrapper.

## Source files used

The algorithm is derived from four lhasa source files:

- `lib/lh5_decoder.c`    — -lh5- decoder configuration and #include harness
- `lib/lh_new_decoder.c` — LHA v2+ ("new style") LZSS+Huffman state machine
- `lib/bit_stream_reader.c` — MSB-first bit-stream I/O
- `lib/tree_decode.c`    — canonical Huffman tree builder and decoder

These four files were combined and the lhasa callback-based I/O layer was
replaced with a simple buffer-cursor adapter, exposing the single public
function:

```c
int lh5_decompress(const uint8_t *src, size_t src_len,
                   uint8_t *dst, size_t dst_len);
```

No algorithmic changes were made.  The I/O adapter and public header are new
code, also released under the ISC license.

## Why not lh5-c?

The preferred source (github.com/samhocevar/lh5-c) returned 404 — the
repository does not exist under that path.  The lhasa fallback was used
instead.  lhasa is well-tested against many real-world LHA archives and is
the canonical open-source LH5 reference implementation.

## API note for downstream tasks

The public API uses a simple buffer-to-buffer signature:

```c
int lh5_decompress(const uint8_t *src, size_t src_len,
                   uint8_t *dst, size_t dst_len);
```

Returns the number of bytes written to `dst`, or -1 on error.
This differs from the plan's assumed signature only in the return type
(`int` not `size_t`) to allow signalling -1 for error.

## Re-merge procedure (if upstream lhasa needs to be re-pulled)

This `lh5.c` is an amalgamation of four lhasa source files. If upstream
lhasa releases a fix that affects LH5 decoding, re-merge by:

1. Clone lhasa at the desired commit: `git clone --depth=1 https://github.com/fragglet/lhasa /tmp/lhasa-src`
2. Copy these four files in order, concatenating into a fresh `lh5.c`:
   - `/tmp/lhasa-src/lib/bit_stream_reader.c` (drop `#include "bit_stream_reader.h"`)
   - `/tmp/lhasa-src/lib/tree_decode.c` (drop `#include "tree_decode.h"`)
   - `/tmp/lhasa-src/lib/lh_new_decoder.c` (drop all `#include`s; keep the function bodies)
   - `/tmp/lhasa-src/lib/lh5_decoder.c` (drop the `LHADecoderType` registration struct at bottom; keep the constants and decoder init)
3. At the top of `lh5.c`, add: `#include "lh5.h"` and an inline definition of
   `typedef size_t (*LHADecoderCallback)(void *buf, size_t buf_len, void *user);`
   (or copy from `lhasa-src/lib/lha_decoder.h`).
4. At the bottom of `lh5.c`, retain or re-add the `BufReader` adapter and the
   public `lh5_decompress` wrapper:

   ```c
   typedef struct { const uint8_t* data; size_t len; size_t pos; } BufReader;
   static size_t buf_reader_cb(void* buf, size_t buf_len, void* user) {
       BufReader* r = (BufReader*)user;
       size_t avail = (r->pos < r->len) ? r->len - r->pos : 0;
       if (buf_len > avail) buf_len = avail;
       memcpy(buf, r->data + r->pos, buf_len);
       r->pos += buf_len;
       return buf_len;
   }
   int lh5_decompress(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len) {
       if (!src || !dst || dst_len == 0) return -1;
       BufReader r = { src, src_len, 0 };
       LHANewDecoder dec;
       if (!lha_lh_new_init(&dec, buf_reader_cb, &r)) return -1;
       size_t written = 0;
       uint8_t outbuf[OUTPUT_BUFFER_SIZE];
       while (written < dst_len) {
           size_t want = dst_len - written;
           if (want > OUTPUT_BUFFER_SIZE) want = OUTPUT_BUFFER_SIZE;
           size_t n = lha_lh_new_read(&dec, outbuf);
           if (n == 0) break;
           if (n > want) n = want;
           memcpy(dst + written, outbuf, n);
           written += n;
       }
       return (int)written;
   }
   ```
   (Adjust to match actual upstream function signatures if they have changed.)
5. Run `cmake --build build/engine && ctest --test-dir build/engine --output-on-failure`
   to verify the amalgamation still decompresses correctly.
6. Update the `at commit <SHA>` line at the top of this file to the new SHA.
