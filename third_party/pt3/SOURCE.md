# pt3 (Vortex Tracker / ProTracker 3 player)

Vendored from https://github.com/Volutar/pt3player at commit aaa5c321466d955b8a96509216a2e5cbc860d7b3
License: MIT (see LICENSE)
Author: Volutar (2021), based on the original PT3 player by Bulba

ZX Spectrum / Vortex Tracker II "PT3" chiptune player.
Produces AY-3-8910 / YM2149 register writes per 50 Hz frame from
PT3 pattern data, instruments, ornaments, and effects.

Used with minor adaptations (see Deviations below).

## Candidate source notes

The plan's preferred source (github.com/true-grue/PT3-decoder) returned 404 —
that repository does not exist. The alternate l29ah/ayfly uses C++ throughout
and has a complex multi-library build. Volutar/pt3player was chosen as the
cleanest pure-C, MIT-licensed PT3 player available.

## Deviations from upstream

1. `pt3player.c` → `pt3.c`, `pt3player.h` → `pt3.h` (rename to match project
   naming convention; `#include "pt3player.h"` updated to `#include "pt3.h"`).
2. `pt3.h` adds `#include <stdint.h>` (needed when included standalone in C11
   translation units).
3. Internal helpers `GetNoteFreq()` and `PatternInterpreter()` and
   `ChangeRegisters()` given `static` linkage to avoid symbol collisions.
4. `func_mute()` was declared but not defined upstream; a minimal body was
   added (zeroes AY registers and disables all channels).
5. `func_restart_music()` now returns 1 (upstream had a missing return value).
6. Unused local variables in `func_play_tick` and `func_restart_music`
   suppressed with `(void)` casts to avoid -Wunused warnings under -Wall.
7. No algorithmic changes.
8. Added TARGET_PLAYDATE guard at top of pt3.c: when defined, suppresses
   `<stdio.h>` include and `#define printf(...) ((void)0)`. Required
   because Playdate's bare-metal Cortex-M7 runtime provides no stdio
   syscalls (_read/_write/etc.). Anyone re-merging upstream must
   preserve this block or the device build will fail to link.

## Public API (after this vendoring)

```c
/* Load a PT3 file into slot chn (0-9).
 * music_ptr: pointer to raw PT3 file bytes
 * length:    byte length of the file
 * ch:        slot index (0-based; up to 10 slots for TS mode)
 * first:     if nonzero, print metadata to stdout
 * Returns number of AY chips (1 for standard, 2/3 for TurboSound). */
int func_setup_music(uint8_t* music_ptr, int length, int ch, int first);

/* Restart playback from position 0 for slot ch. */
int func_restart_music(int ch);

/* Advance slot ch by one 50 Hz tick; updates internal AY[14] array. */
void func_play_tick(int ch);

/* Copy the 14 AY register bytes for slot ch into dest[14]. */
void func_getregs(uint8_t *dest, int ch);

/* Zero all AY registers and disable all channels (all slots). */
void func_mute(void);

/* Override note frequency table (-1 = use file-specified table, 0-6 = force). */
extern int forced_notetable;
```

## Re-merge procedure (if upstream is updated)

1. `git clone --depth=1 https://github.com/Volutar/pt3player /tmp/pt3-src`
2. Copy `pt3player.c` → `third_party/pt3/pt3.c` and `pt3player.h` →
   `third_party/pt3/pt3.h`.
3. Re-apply the deviations listed above (rename include, add `<stdint.h>`,
   add `static` to internal helpers, add `func_mute` body, fix
   `func_restart_music` return, add `(void)` suppressions).
4. Run `cmake --build build/engine && ctest --test-dir build/engine
   --output-on-failure` to verify the build still passes.
5. Update the commit SHA at the top of this file.
