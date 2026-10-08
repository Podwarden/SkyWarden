# AY-3-8910 / YM2149 chip emulator (true-grue / ayumi)

Vendored from https://github.com/true-grue/ayumi at commit
`07c08b4874c359169e4a028edf73f046d8b763e2` (HEAD as of 2026-06-06).
License: MIT (Peter Sovietov; see `LICENSE`).

Used **only** by the reference renderer `tools/ayref/` for comparison
against our production `ay_mini` integer-only emulator. Not linked into
the engine itself, not shipped to the Playdate.

## Files copied verbatim

| File       | Purpose                                          |
|------------|--------------------------------------------------|
| `ayumi.h`  | Public API: `struct ayumi`, setters, process     |
| `ayumi.c`  | Implementation (FIR-decimated, DC-filtered, FP) |
| `LICENSE`  | MIT license text                                 |

## What ayumi is

A floating-point reference-quality AY-3-8910 / YM2149 PSG chip emulator
widely cited as the most accurate modern OSS implementation. Renders
**stereo** at a host sample rate by running the chip at its native
1.7734 MHz (Spectrum 128K) clock and decimating through a 192-tap FIR
to the output sample rate. Includes a per-channel DC-removal filter,
EQP-style stereo panning, and (in its `update_state` helper) the
standard AY register decoding.

Notable contrast with our `ay_mini`:

- **FP throughout** — uses `double` for DAC table + decimation.
- **High-quality decimation** — 192-tap FIR at oversample factor 8,
  vs `ay_mini`'s simple per-sample accumulator.
- **DC filter is per-channel pre-mix**, sized at 1024 samples.
- **Stereo output** (L+R via panning) — we collapse to mono for the
  fairness of comparison.

## Integration

The renderer is `tools/ayref/ayref.c`. It links its own private copy of
`third_party/z80emu/z80emu.c` compiled with a SEPARATE `z80user.h`
(`tools/ayref/z80user.h`) that routes bus thunks to ayref's own
machine struct — so production engine internals are untouched.

The .ay parsing + Z80 driving logic is adapted from
`engine/src/formats/ay.c` and `engine/src/formats/ay_z80_glue.c`. That
isolates the variable being measured to the **chip emulator** alone:
both engines see identical Z80 execution, identical AY register
streams, and differ only in how those registers are turned into audio.

## Re-vendoring

```
git clone --depth=1 https://github.com/true-grue/ayumi /tmp/ayumi-src
cp /tmp/ayumi-src/ayumi.{c,h} /tmp/ayumi-src/LICENSE third_party/ayumi/
git -C /tmp/ayumi-src rev-parse HEAD     # update SHA above
```
