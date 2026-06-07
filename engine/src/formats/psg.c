#include "formats/psg.h"
#include <string.h>

int psg_open(PsgFile* psg, const uint8_t* buf, size_t len) {
    if (len < 16 || memcmp(buf, "PSG\x1a", 4) != 0) return 0;
    psg->data          = buf;
    psg->len           = len;
    psg->version       = buf[4];
    /* Frequency byte: 0 means 50 Hz; otherwise N*50 Hz (rarely non-zero). */
    psg->frame_rate_hz = (buf[5] == 0) ? 50 : (uint16_t)(buf[5] * 50);
    psg->cursor        = 16;
    psg->skip_frames   = 0;
    return 1;
}

void psg_close(PsgFile* psg) {
    psg->data = NULL;
    psg->len = 0;
}

int psg_next_frame(PsgFile* psg, uint8_t* regs) {
    /* Honour pending skip (frames of identical state). */
    if (psg->skip_frames > 0) { psg->skip_frames--; return 1; }

    while (psg->cursor < psg->len) {
        uint8_t b = psg->data[psg->cursor++];
        if (b == 0xFF) {
            /* end of frame, advance one */
            return 1;
        } else if (b == 0xFE) {
            /* end of frame + N*4 silent frames */
            if (psg->cursor >= psg->len) return 1;
            uint8_t n = psg->data[psg->cursor++];
            psg->skip_frames = (uint16_t)(n * 4);
            return 1;
        } else {
            /* register write: register index then value */
            if (psg->cursor >= psg->len) return 0;
            uint8_t value = psg->data[psg->cursor++];
            if (b < 14) regs[b] = value;
            /* registers >= 14 are reserved/ignored per spec */
        }
    }
    return 0; /* end of stream */
}

#include "format_vtable.h"

static int psg_open_vt(void* state, const uint8_t* buf, size_t len, int* fr_out) {
    PsgFile* psg = (PsgFile*)state;
    if (!psg_open(psg, buf, len)) return 0;
    *fr_out = psg->frame_rate_hz;
    return 1;
}

static int psg_next_frame_vt(void* state, uint8_t* regs) {
    return psg_next_frame((PsgFile*)state, regs);
}

static void psg_close_vt(void* state) {
    psg_close((PsgFile*)state);
}

const FormatVTable psg_vtable = {
    .open       = psg_open_vt,
    .next_frame = psg_next_frame_vt,
    .close      = psg_close_vt,
    .set_position = NULL,        /* PSG has no order list to seek */
    .state_size = sizeof(PsgFile),
};
