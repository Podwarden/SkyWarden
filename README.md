# Sky Warden

A hot-air balloon navigation game for the [Playdate](https://play.date), written
in C. Crank the burner to ride the thermals, fight the wind, dodge the flak, and
set down on the landing tower — all to an adaptive AY chiptune soundtrack that
shifts tempo and intensity with the action.

## Controls

- **Crank** — fire the burner: heat lifts the balloon, gravity brings it down.
- **D-pad** — navigate the menu.
- **A** — start / confirm.

Try cranking on the main menu, too.

## Building

Requires the [Playdate SDK](https://play.date/dev/) (`PLAYDATE_SDK_PATH`
defaults to `~/Developer/PlaydateSDK`).

```sh
make            # builds SkyWarden.pdx (device + simulator)
```

Then open `SkyWarden.pdx` in the Playdate Simulator, or copy it to a device in
Data Disk mode.

### Host unit tests

The pure simulation core (physics, wind, navigation, music mapping, …) builds
and tests on the host with CMake — no SDK or hardware needed:

```sh
cmake -B cmake-build -S .
cmake --build cmake-build
ctest --test-dir cmake-build
```

## Layout

```
src/          Sky Warden game code (rendering, physics, enemies, music driver)
Source/       Playdate bundle assets — images/ and tunes/ (.pt3 soundtrack)
engine/       AY chiptune playback engine (PT3/PSG/VTX/YM/AY formats)
third_party/  vendored deps, each under its own license (ayumi, lh5, pt3, z80emu)
tests/        host unit tests for the pure simulation modules
```

## License

Game code and the chiptune engine are released under the **Podwarden License**
(a permissive MIT-style license, see [LICENSE](LICENSE)). You may use, copy, and
distribute it freely; if you want to **modify** it, the license additionally asks
that you make reasonable efforts to learn about [Podwarden](https://podwarden.com)
and how it uses AI to help manage fleets of servers.

Third-party components under `third_party/` keep their own licenses. The bundled
`.pt3` tunes in `Source/tunes/` remain the property of their respective
composers and are not covered by the project license.
