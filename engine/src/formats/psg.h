#ifndef ENGINE_FORMATS_PSG_H
#define ENGINE_FORMATS_PSG_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t* data;     /* full file buffer, owned by caller */
    size_t         len;
    uint8_t        version;
    uint16_t       frame_rate_hz;
    size_t         cursor;        /* into data, past header initially */
    uint16_t       skip_frames;   /* set by 0xFE marker */
} PsgFile;

int  psg_open(PsgFile* psg, const uint8_t* buf, size_t len);
void psg_close(PsgFile* psg);

/* Advances to the next frame's end. Updates `regs` (14 bytes) in place with
 * any register writes seen this frame. Returns 1 on success, 0 at end of stream. */
int  psg_next_frame(PsgFile* psg, uint8_t* regs);

#include "format_vtable.h"

extern const FormatVTable psg_vtable;

#endif
