# F-Zero Recomp

A native, widescreen, static recompilation of F-Zero (USA).

You supply your own cartridge dump; ROMs and generated game code are not included.

<p align="center">
  <a href="assets/screenshots/title-screen.jpg"><img src="assets/screenshots/title-screen.jpg" width="32%" alt="F-Zero title screen in widescreen"></a>
  <a href="assets/screenshots/mute-city-i-start.jpg"><img src="assets/screenshots/mute-city-i-start.jpg" width="32%" alt="The opening moments of a race on Mute City I"></a>
  <a href="assets/screenshots/mute-city-ii-start.jpg"><img src="assets/screenshots/mute-city-ii-start.jpg" width="32%" alt="The opening moments of a race on Mute City II"></a>
</p>

## Status and features

Most of the game has been tested
on macOS with Apple Silicon. Windows and Linux builds are not yet verified.
Development is ongoing; not every course, vehicle, or game situation is covered.

- Optional 16:9 widescreen
- Enhanced visual filters
- Keyboard and gamepad input, audio, and persistent saves
- ROM picker with identity verification
- In-game display, audio, and input settings

Filters currently require macOS, SDL 3.4 or newer, and the Metal renderer. Other
renderers retain Original colours and support widescreen. Unsupported game
layouts use the native view.

## ROM requirements

Use a **headerless F-Zero (USA)** cartridge dump for code generation:

| Property | Value |
| --- | --- |
| Size | 524,288 bytes |
| Mapping | LoROM |
| SHA-256 | `bf16c3c867c58e2ab061c70de9295b6930d63f29f81cc986f5ecae03e0ad18d2` |

The launcher also accepts the same payload with a 512-byte copier header.

## Build and run

Requirements: Git, Python 3.11+, CMake 3.20+, Ninja, C11/C++17 compilers, SDL3
development files, and OpenGL development files. On macOS, Apple's Command Line
Tools provide the compilers and OpenGL SDK; Homebrew can supply the other tools:

```sh
brew install cmake ninja python sdl3
```

Clone the project, then place your headerless dump at `fzero_usa_reference.sfc`
in the project directory before generation:

```sh
git clone https://github.com/craigshaw/FZeroRecomp.git
cd FZeroRecomp
git submodule update --init
sh tools/apply_snesrecomp_patches.sh
python3 snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir config --out config/funcs.h
python3 snesrecomp/tools/v2_emit.py --rom fzero_usa_reference.sfc --cfg-dir config --out-dir generated --cfg-roots
sh build.sh
./build/fzero_recomp
```

Choose the ROM in the launcher and press Play. Its path is remembered. To skip
the launcher for one run, pass a ROM path:

```sh
./build/fzero_recomp "/path/to/F-Zero (USA).sfc"
```

On Windows, install the requirements separately, use `python` for generation,
and run `./build.ps1` from PowerShell instead of `sh build.sh`. It does not
install dependencies or generate game code. The executable is
`build/fzero_recomp.exe`. This path remains unverified.

## Controls and settings

| Control | Keyboard |
| --- | --- |
| D-pad | Arrow keys |
| A / B / X / Y | X / Z / S / A |
| L / R | Q / E |
| Start / Select | Enter / Backspace |
| Settings | F1 |
| FPS readout | F |
| Quit, with settings closed | Esc |

Connect a gamepad before launch and select Gamepad as the input source. The
host uses the first available pad, with standard SNES button positions and
D-pad or left-stick steering. Custom physical pad bindings and pad selection
are not implemented. Keyboard bindings can be changed in the launcher's
Controller page and are loaded when you press Play.

F1 or gamepad Select+Start opens settings and pauses gameplay and audio.
**Display** contains Widescreen, Visual Style, and FPS Readout. The launcher's
generic save-state, rewind, and reset shortcuts are not connected to this host.

Keep the executable and its `assets/` folder together in a writable directory.
Settings live beside the executable: `config.ini`, `keybinds.ini`, and `rom.cfg`.
SRAM is saved to `saves/save.srm` on normal exit. Close the game before renaming
a settings file to restore its defaults; leave the save file in place.

If the ROM is rejected, check its region, size, and hash. If CMake cannot find
SDL3, install its development files and set `SDL3_DIR` to the folder containing
`SDL3Config.cmake`. If input does not respond, check the selected input source.

## Development

See [architecture](docs/ARCHITECTURE.md) and [dependency patches](docs/SNESRECOMP_PATCHES.md).
Report bugs with the platform, build revision, settings, and reproduction steps.
Do not attach ROMs or generated game code.

## Development disclosure

AI coding assistants have contributed to the runtime, tooling, testing, and
documentation under the maintainer's direction and review.

## Credits and licence

Built on [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp),
[recomp-ui](https://github.com/mstan/recomp-ui), and the projects credited by
those dependencies. See [third-party notices](docs/THIRD_PARTY_NOTICES.md).

Original code in this repository uses the [PolyForm Noncommercial License 1.0.0](LICENSE).

Required Notice: Copyright (c) 2026 Craig Shaw

Third-party code retains its respective licences. The ROM, generated game code,
and game artwork are not covered by the project licence. The gameplay screenshots
illustrate the project; the artwork shown belongs to its respective rights holders.
This project is not affiliated with or endorsed by Nintendo. *F-Zero* and its
related names and assets belong to their respective owners.
