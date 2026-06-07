#ifndef ENGINE_FORMATS_PT3_H
#define ENGINE_FORMATS_PT3_H

#include <stddef.h>
#include <stdint.h>
#include "format_vtable.h"

/* Wraps the vendored Volutar/pt3player in our vtable interface.
 *
 * IMPORTANT: the vendored player uses module-level globals — it is not
 * reentrant. Only one Pt3File may be loaded at a time across the whole
 * process. This is fine for our use case (audio thread plays one tune)
 * but worth knowing if the player is ever multiplexed. */

typedef struct Pt3File {
    /* Pointer to the tune buffer kept alive by engine_load's caller; the
     * vendored player retains pointers into this so we can't free it. */
    const uint8_t* buf;
    size_t         len;

    /* Bookkeeping. */
    uint32_t       frame_rate_hz;   /* always 50 for PT3 */
    int            loaded;          /* 1 once func_setup_music returned > 0 */
} Pt3File;

int  pt3_open(Pt3File* p, const uint8_t* buf, size_t len);
void pt3_close(Pt3File* p);
int  pt3_next_frame(Pt3File* p, uint8_t* regs);

extern const FormatVTable pt3_vtable;

#endif
