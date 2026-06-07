/* z80user.h — LOCAL EDIT
 *
 * This file replaces upstream's ZEXTEST scaffolding with hooks into our
 * Playdate AY chiptune glue (engine/src/formats/ay_z80_glue.{h,c}).
 *
 * Upstream copyright (c) 2016, 2017 Lin Ke-Fong — "This code is free,
 * do whatever you want with it."
 *
 * Macros run inside z80emu.c (a separate translation unit), so they call
 * out to plain extern thunks defined in ay_z80_glue.c. The `context`
 * variable provided by the emulator is a `void*` we set to `AyZ80*` when
 * invoking Z80Emulate/Z80Interrupt.
 */

#ifndef __Z80USER_INCLUDED__
#define __Z80USER_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct AyZ80;
uint8_t ay_z80_bus_read(struct AyZ80* z, uint16_t addr);
void    ay_z80_bus_write(struct AyZ80* z, uint16_t addr, uint8_t v);
uint8_t ay_z80_bus_in(struct AyZ80* z, uint16_t port);
void    ay_z80_bus_out(struct AyZ80* z, uint16_t port, uint8_t v);

/* ---- Memory access ---- */

#define Z80_READ_BYTE(address, x)                                              \
    do { (x) = ay_z80_bus_read((struct AyZ80*)context, (uint16_t)(address)); } \
    while (0)

#define Z80_FETCH_BYTE(address, x)  Z80_READ_BYTE((address), (x))

#define Z80_READ_WORD(address, x)                                              \
    do {                                                                       \
        uint16_t _a = (uint16_t)(address);                                     \
        uint8_t  _lo = ay_z80_bus_read((struct AyZ80*)context, _a);            \
        uint8_t  _hi = ay_z80_bus_read((struct AyZ80*)context, (uint16_t)(_a + 1)); \
        (x) = (unsigned)_lo | ((unsigned)_hi << 8);                            \
    } while (0)

#define Z80_FETCH_WORD(address, x)  Z80_READ_WORD((address), (x))

#define Z80_WRITE_BYTE(address, x)                                             \
    ay_z80_bus_write((struct AyZ80*)context, (uint16_t)(address), (uint8_t)(x))

#define Z80_WRITE_WORD(address, x)                                             \
    do {                                                                       \
        uint16_t _a  = (uint16_t)(address);                                    \
        unsigned _vv = (unsigned)(x);                                          \
        ay_z80_bus_write((struct AyZ80*)context, _a, (uint8_t)(_vv));          \
        ay_z80_bus_write((struct AyZ80*)context, (uint16_t)(_a + 1),           \
                         (uint8_t)(_vv >> 8));                                 \
    } while (0)

#define Z80_READ_WORD_INTERRUPT(address, x)   Z80_READ_WORD((address), (x))
#define Z80_WRITE_WORD_INTERRUPT(address, x)  Z80_WRITE_WORD((address), (x))

/* ---- Port I/O ---- */

#define Z80_INPUT_BYTE(port, x)                                                \
    do { (x) = ay_z80_bus_in((struct AyZ80*)context, (uint16_t)(port)); }      \
    while (0)

#define Z80_OUTPUT_BYTE(port, x)                                               \
    ay_z80_bus_out((struct AyZ80*)context, (uint16_t)(port), (uint8_t)(x))

#ifdef __cplusplus
}
#endif

#endif
