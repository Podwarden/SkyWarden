# Z80 CPU emulator (anotherlin / z80emu)

Vendored from https://github.com/anotherlin/z80emu at commit 1c418fa0d719abab9273131113defbe276101d95
(tag z80emu-1.1.3, 2017-09-18).
License: per-file "This code is free, do whatever you want with it." (see LICENSE)
Author: Lin Ke-Fong, 2012-2017

Instruction-stepped Z80 CPU emulator. Passes both zexdoc and zexall.
Uses a generic-instruction + decode-table approach (decoded once into
`register_table[]` pointers at reset) which keeps the hot path compact —
intended to fit inside the L1 instruction cache.

Selected here over the cycle-stepped floooh/chips Z80 emulator because
chiptune playback only needs OUT-port write capture, not per-cycle bus
accuracy. Instruction-stepped emulators do considerably less work per
emulated Z80 second on M-class cores; on Cortex-M7 the cycle-stepped
chips Z80 measured ~560 kHz effective vs. the 3.5 MHz target.

## Files

| File             | Purpose                                                                        |
|------------------|--------------------------------------------------------------------------------|
| `z80emu.h`       | Public API: `Z80_STATE`, `Z80Reset`, `Z80Emulate`, `Z80Interrupt`              |
| `z80emu.c`       | Implementation (~2.6 kLoC)                                                     |
| `z80config.h`    | Compile-time feature flags (Z80_CATCH_HALT etc., all off here)                 |
| `z80user.h`      | **EDITED.** Host integration macros (memory + I/O). Replaced upstream's        |
|                  | ZEXTEST scaffolding with our AY glue (`ay_z80_glue.c`).                        |
| `instructions.h` | Generic-instruction enum                                                       |
| `macros.h`       | Internal helper macros                                                         |
| `tables.h`       | Decode tables (precomputed)                                                    |

## Integration notes

- Host is little-endian (Cortex-M7, x86) — no `Z80_BIG_ENDIAN` define.
- **`Z80_CATCH_HALT` is enabled** via `z80config.h`. We rely on it so that
  the player's `EI; HALT; <wait>` main loop stops emulation when HALT is
  reached, the glue notes "halted", and the next INT delivery (via
  `Z80Interrupt`) wakes the CPU. Without this, z80emu's HALT instead burns
  the remaining cycle budget and falls through to the post-HALT instruction
  — breaking chiptune playback. Upstream z80emu.c references
  `Z80_STATUS_FLAG_HALT` / `_DI` / `_EI` / `_RETI` / `_RETN` / `_ED_UNDEFINED`
  but defines only `Z80_STATUS_HALT` etc. in the header — we alias the
  `_FLAG_` symbols in `z80config.h` so the code compiles when the catches
  are enabled.
- `z80user.h` is **the** customization point; we rewrote it to route through
  `ay_z80_bus_read`, `ay_z80_bus_write`, `ay_z80_bus_in`, `ay_z80_bus_out`
  (plain extern thunks defined in `ay_z80_glue.c`; the macros are expanded
  inside `z80emu.c` which is a separate TU).
- **Local edits to `z80emu.c`**: upstream's IN/OUT (C),r passes only `C` (8-bit)
  to `Z80_INPUT_BYTE`/`Z80_OUTPUT_BYTE`. ZX Spectrum AY decoding reads A15/A14
  from B, so we pass `BC` (full 16-bit). Six call sites edited (3 IN, 3 OUT).
  IN A,(n) and OUT (n),A also patched to put A in the upper byte, matching
  Zilog hardware behavior. Search z80emu.c for `LOCAL EDIT` to find them.
- **HALT PC rollback**: when Z80Emulate returns with `status==HALT`, PC has
  been incremented past the HALT byte. The glue rolls PC back by 1 so that
  `Z80Interrupt` saves the pre-HALT PC on the stack — RETI then naturally
  resumes at the post-HALT instruction (matching real Z80 behavior). Search
  `ay_z80_glue.c` for "roll back".

## Build

`z80emu.c` is compiled as a separate translation unit. For the macros in
`z80user.h` to call our `static inline` helpers, the glue helpers are
exposed via plain `extern` thunks declared in `ay_z80_glue.h` and
defined in `ay_z80_glue.c`.

```cmake
add_library(z80 STATIC third_party/z80emu/z80emu.c)
target_include_directories(z80 PUBLIC third_party/z80emu)
```

The `context` passed to `Z80Emulate(...)` is a `(void*)` pointer to the
host `AyZ80*`.

## Public API summary

```c
/* Reset to power-on state. Wires up internal register_table[] pointers
 * INTO the same Z80_STATE struct, so the state must not be copied/moved
 * after Z80Reset is called. */
void Z80Reset(Z80_STATE* state);

/* Execute until elapsed_cycles >= number_cycles, then complete the current
 * instruction and return. Return value is the cycle count actually consumed
 * (may slightly overshoot number_cycles). */
int  Z80Emulate(Z80_STATE* state, int number_cycles, void* context);

/* If IFF1 is set, accept a maskable interrupt of the configured mode.
 * In IM 1 (what ZX Spectrum AY players use): push PC, jump to 0x0038,
 * return 13 cycles. If IFF1 is clear, returns 0 and the interrupt is lost
 * (caller must retry). */
int  Z80Interrupt(Z80_STATE* state, int data_on_bus, void* context);
```

## Z80_STATE size

On 32-bit ARM (Cortex-M7) `sizeof(Z80_STATE)` is ~244 bytes — dominated by
three 16-entry pointer tables. Too large to embed directly in the engine's
256-byte per-format state buffer, so we keep it heap-allocated behind a
`void* cpu` in `AyZ80` (same pattern as the old chips-based glue).

## Re-merge procedure

1. `git clone --depth=1 https://github.com/anotherlin/z80emu /tmp/z80emu-src`
2. Copy the seven files listed above into `third_party/z80emu/`, then
   re-apply the local edit to `z80user.h` (replace the ZEXTEST scaffolding
   with the AY glue macros — diff this directory's `z80user.h` against
   upstream to see what we changed).
3. Update the commit SHA at the top of this file.
4. Run `cmake --build build/engine && ctest --test-dir build/engine`.
