#ifndef ENGINE_FORMATS_YM_H
#define ENGINE_FORMATS_YM_H

#include <stddef.h>
#include <stdint.h>
#include "format_vtable.h"

typedef struct {
    int            variant;          /* 2, 3, 5, or 6 */
    uint32_t       frame_count;
    uint32_t       frame_rate_hz;
    uint32_t       loop_frame;
    /* Column-major bytes: streams[r * frame_count + f] is register r at frame f.
     * For LHA-wrapped YM: streams points into owned_buf.
     * For raw YM: streams borrows the caller's buffer and owned_buf is NULL. */
    const uint8_t* streams;
    uint32_t       cursor;
    uint8_t*       owned_buf;   /* malloc'd decompressed bytes for LHA path, else NULL */
} YmFile;

int  ym_open(YmFile* ym, const uint8_t* buf, size_t len);
void ym_close(YmFile* ym);
int  ym_next_frame(YmFile* ym, uint8_t* regs);

extern const FormatVTable ym_vtable;

#endif
