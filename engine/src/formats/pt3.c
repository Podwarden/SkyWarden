#include "formats/pt3.h"
#include <pt3.h>            /* from third_party/pt3 — angle brackets to avoid
                             * resolving to our own engine/src/formats/pt3.h */
#include <string.h>

/* Adapter notes:
 *   - The vendored player (Volutar/pt3player, MIT) exposes
 *     func_setup_music / func_play_tick / func_getregs which write to an
 *     internal AY register buffer per chip index.
 *   - We use chip 0 only; standard PT3 is single-chip mono.
 *   - GLOBAL STATE WARNING: the vendored player stores all playback state
 *     in module-level globals. Only one Pt3File may be open at a time across
 *     the entire process. Instantiating a second Pt3File without closing the
 *     first will silently corrupt both players. This is documented and
 *     acceptable because the engine plays one tune at a time on one thread. */

int pt3_open(Pt3File* p, const uint8_t* buf, size_t len) {
    memset(p, 0, sizeof(*p));
    /* Vendored func_setup_music reads (TSData*)(buf + len - 22) immediately
     * without bounds-checking length. Reject anything too small to plausibly
     * be a PT3 (the 100-byte ASCII title block + minimal patterns/samples
     * means real PT3 files are always > 200 bytes; use 32 as the hard
     * minimum to cover the TSData read without rejecting tiny test fixtures). */
    if (!buf || len < 32) return 0;

    /* Cast away const — vendor API takes uint8_t* but is logically read-only;
     * it keeps pointers into the buffer so the caller must keep it alive. */
    int n_chips = func_setup_music((uint8_t*)buf, (int)len, /*ch*/0, /*first*/1);
    if (n_chips <= 0) return 0;

    p->buf           = buf;
    p->len           = len;
    p->frame_rate_hz = 50;
    p->loaded        = 1;
    return 1;
}

void pt3_close(Pt3File* p) {
    if (p->loaded) {
        func_mute();
        p->loaded = 0;
    }
    p->buf = NULL;
    p->len = 0;
}

int pt3_next_frame(Pt3File* p, uint8_t* regs) {
    if (!p->loaded) return 0;
    /* Step one 50 Hz frame, then read register state out. */
    func_play_tick(0);
    func_getregs(regs, 0);
    /* The vendored player loops automatically inside func_play_tick — it never
     * "ends" in a way that next_frame would need to handle. So we always
     * return 1 (success / keep going). The engine's rewind path (open()
     * called from advance_one_frame on end-of-stream) is dead for PT3. */
    return 1;
}

/* Vtable adapter ===================================================== */

static int pt3_open_vt(void* state, const uint8_t* buf, size_t len, int* fr_out) {
    Pt3File* p = (Pt3File*)state;
    if (!pt3_open(p, buf, len)) return 0;
    *fr_out = (int)p->frame_rate_hz;
    return 1;
}
static int pt3_next_frame_vt(void* state, uint8_t* regs) {
    return pt3_next_frame((Pt3File*)state, regs);
}
static void pt3_close_vt(void* state) {
    pt3_close((Pt3File*)state);
}
static int pt3_set_position_vt(void* state, int pos) {
    Pt3File* p = (Pt3File*)state;
    if (!p->loaded) return 0;
    /* The engine drives the vendored player on chip 0 only (see func_play_tick(0)
     * / func_getregs(regs, 0) above); seek the same chip. pt3_set_position clamps
     * pos to the tune's valid position range. */
    pt3_set_position(0, pos);
    return 1;
}
static int pt3_get_position_vt(const void* state) {
    const Pt3File* p = (const Pt3File*)state;
    if (!p->loaded) return -1;
    /* Read the same chip (0) the engine drives. Returns -1 if unavailable. */
    return pt3_get_position(0);
}

const FormatVTable pt3_vtable = {
    .open       = pt3_open_vt,
    .next_frame = pt3_next_frame_vt,
    .close      = pt3_close_vt,
    .set_position = pt3_set_position_vt,
    .get_position = pt3_get_position_vt,
    .state_size = sizeof(Pt3File),
};
