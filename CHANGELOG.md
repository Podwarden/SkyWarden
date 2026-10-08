# Changelog

All notable changes to Sky Warden are recorded here.

## [1.0.1]

- Relicensed under the standard **MIT License** (Copyright (c) 2026 PodWarden),
  replacing the earlier MIT-style "Podwarden License". Third-party components
  under `third_party/` keep their own licenses, now listed in the README.
- The game's metadata (`Source/pdxinfo`) now lists PodWarden as the author and
  carries `version=1.0.1` and a `buildNumber`, which the Playdate needs to
  offer sideload updates.
- Cleaned release build: the `SkyWarden-v1.0.1.pdx.zip` asset is built from a
  fresh checkout with no local file paths embedded, no macOS `._*` resource
  files, and without the simulator-only `pdex.dylib` (the device runs
  `pdex.bin`). It replaces the v1.0.0 zip.
- README: release recipe, sideload notes, license section, and a short note at
  the bottom about who makes the game.
- No gameplay changes.

## [1.0.0] - 2026-06-07

First public release.

- Hot-air balloon navigation game for the Playdate, written in C.
- Crank-driven burner physics; steering by climbing or sinking into wind
  layers that blow left, right, or not at all.
- Flak and missiles to dodge, and a landing tower to set down on.
- Adaptive AY chiptune soundtrack (PT3) that follows the action, played by a
  bundled chiptune engine.
- Title menu, About screen and an interactive tutorial.
- Host unit tests for the simulation core (CMake, no SDK needed).
- Deterministic gameplay capture (`capture.sh`) that produced the README's
  GIF, MP4 and screenshots.
