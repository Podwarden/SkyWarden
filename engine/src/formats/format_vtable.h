#ifndef ENGINE_FORMATS_VTABLE_H
#define ENGINE_FORMATS_VTABLE_H

#include <stddef.h>
#include <stdint.h>

/* A format implementation provides three functions: open, next_frame, close.
 * The `state` is opaque to engine.c; each format owns its own struct. */
typedef struct FormatVTable {
    /* Open: parse header, set up state. Returns 1 on success, 0 on parse failure.
     * On success, *frame_rate_hz_out is set to the file's tick rate (typically 50). */
    int (*open)(void* state, const uint8_t* buf, size_t len, int* frame_rate_hz_out);

    /* Advance one frame. Updates `regs` (14 bytes) in place with any register writes.
     * Returns 1 on success, 0 at end of stream (engine handles looping). */
    int (*next_frame)(void* state, uint8_t* regs);

    /* Close: release any resources held by the format's state. */
    void (*close)(void* state);

    /* Optional: seek the sequencer to order-list position `pos`. NULL for
     * formats that don't support seeking (the engine treats NULL as a no-op).
     * Currently only PT3 implements this. */
    int (*set_position)(void* state, int pos);

    /* Optional: read the sequencer's current order-list position. NULL for
     * formats that don't support it (the engine reports -1). Currently only
     * PT3 implements this. */
    int (*get_position)(const void* state);

    /* Size of the per-format state struct (for engine.c to embed it). */
    size_t state_size;
} FormatVTable;

#endif
