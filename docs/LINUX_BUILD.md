# Linux source build

The native Release build has passed all 11 CTest tests on Ubuntu 24.04 x86_64.
A windowed Wayland run completed 1,800 game frames with OpenGL presentation
and 1,796 successful full-frame RGB readback comparisons. This is an initial
build and startup check. Windowed and fullscreen timing checks also passed on
this machine, as described below. A manual play test with an Xbox One S
Controller found no gameplay, audio, or settings issues. Screenshots and save
writes were checked, and records and settings survived a relaunch.
Portability to other distributions still needs validation.
There is no packaged Linux release yet.

Linux currently uses SDL scene and HUD composition with Original colours.
The Enhanced, Vivid, and Black & White shader path is implemented for macOS
and Windows only. Installing a Vulkan driver does not enable those filters
on Linux. Original, 16:9, and 32:9 remain available.

## Install build prerequisites

On Ubuntu 24.04, install the compiler, build tools, and development headers:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends \
  build-essential git curl python3 cmake ninja-build pkg-config \
  libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libwayland-dev wayland-protocols libdecor-0-dev \
  libxkbcommon-dev libx11-dev libxext-dev libxrandr-dev \
  libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev \
  libdrm-dev libgbm-dev libgl-dev libegl-dev libgles-dev \
  libdbus-1-dev libudev-dev libusb-1.0-0-dev libvulkan-dev \
  mesa-utils vulkan-tools
```

These headers enable SDL's Wayland, X11, OpenGL, Vulkan, audio, and controller
backends. See [SDL's Linux build guidance](https://wiki.libsdl.org/SDL3/README-linux)
for other distributions. Verify graphics acceleration from a desktop terminal:

```sh
glxinfo -B
vulkaninfo --summary
```

Check that the selected hardware is present. A list that includes `llvmpipe`
alongside the GPU does not by itself mean the game is using software rendering.

## Build SDL locally

Run the following commands from the FZeroRecomp project directory.
This build uses SDL 3.4.16, matching the Windows build and the initial Linux
performance report. The shared library and its headers stay under ignored
`.tools/`; installation does not require sudo.

```sh
mkdir -p .tools/downloads .tools/src
curl --fail --location --retry 3 \
  --output .tools/downloads/SDL3-3.4.16.tar.gz \
  https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
printf '%s\n' \
  '7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68  .tools/downloads/SDL3-3.4.16.tar.gz' \
  | sha256sum --check -
```

Continue only if the checksum reports `OK`:

```sh
tar -xzf .tools/downloads/SDL3-3.4.16.tar.gz -C .tools/src
cmake -S .tools/src/SDL3-3.4.16 -B .tools/build/sdl3-3.4.16 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/.tools/sdl3-3.4.16" \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF \
  -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
cmake --build .tools/build/sdl3-3.4.16 --parallel 12
cmake --install .tools/build/sdl3-3.4.16
```

Reduce the parallel job count on machines with less memory. In SDL's configure
summary, check that the required video and audio backends are enabled.

## Generate and build the game

Use the pinned dependencies and the headerless USA ROM described in the
[main README](../README.md#rom-requirements). The ROM can remain outside the
checkout. Set `FZERO_ROM` to its absolute path:

```sh
git submodule update --init
sh tools/apply_snesrecomp_patches.sh
FZERO_ROM="/absolute/path/to/F-Zero (USA).sfc"
sha256sum "$FZERO_ROM"
```

The ROM must be 524,288 bytes, with SHA-256
`bf16c3c867c58e2ab061c70de9295b6930d63f29f81cc986f5ecae03e0ad18d2`.
After checking it, generate and build:

```sh
python3 snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir config --out config/funcs.h
python3 snesrecomp/tools/v2_emit.py --rom "$FZERO_ROM" \
  --cfg-dir config --out-dir generated --cfg-roots
CMAKE_PREFIX_PATH="$PWD/.tools/sdl3-3.4.16" sh build.sh
ctest --test-dir build --output-on-failure
ldd build/fzero_recomp
./build/fzero_recomp "$FZERO_ROM"
```

Generation produces 678 exact AOT variants and 9 interpreter variants at the
current dependency pin. `ldd` must resolve `libSDL3.so.0` to the local `.tools`
installation and show no missing libraries. CMake records that SDL path in
`build/CMakeCache.txt`; later source edits can use `sh build.sh` directly.
If the project is moved, reconfigure it with the new SDL path.

Run `./build/fzero_recomp` without a ROM argument to open the launcher.
Keep `build/assets/` beside the executable. Source builds store settings and
the default saves beside the executable, as described in the main README.
This build directory depends on the local SDL installation and is not a
portable release package.

## Initial validation environment

The first check used GCC 13.3.0, Python 3.12.3, CMake 3.28.3, Ninja 1.11.1,
and SDL 3.4.16 on Ubuntu 24.04.5 with kernel 7.0.0-31-generic. The machine had
an Intel Core i9-12900HK, Iris Xe graphics using `i915`, 32 GB RAM, and Mesa
25.2.8. OpenGL reported hardware acceleration; Vulkan listed the Intel GPU.

The startup check used a copy of the executable with separate settings and
saves, a 1,194 x 672 window, 16:9, Original colours, Wayland, and these variables:

```sh
SDL_VIDEODRIVER=wayland \
SNESRECOMP_MAX_FRAMES=1800 \
SNESRECOMP_VALIDATE_PRESENTATION=1 \
  ./fzero_recomp "$FZERO_ROM"
```

It exited normally in about 30 seconds using SDL's `opengl` renderer. Audio
was enabled but muted. The RGB checks validate frame composition, not manual
gameplay or visible correctness of every scene. Readback adds work, so this
run is not a performance baseline. Use the existing
[frame timing diagnostics](ARCHITECTURE.md#diagnostics) for performance work.

## Native Linux timing baseline

On 17 October 2026, four runs of the same 3,600-frame attract sequence used
the Release build, SDL 3.4.16, Wayland, and SDL's `opengl` renderer on the
machine above. The game opened the Intel GPU's `/dev/dri/renderD128` device.
SDL reported a 60 Hz display. Each run started with separate, empty saves and
the same settings: 16:9, Original colours, nearest-neighbour filtering, and
audio enabled at 48 kHz with volume zero. Readback validation and the FPS
overlay were off. Gameplay input was disabled for these comparisons.

All four runs exited normally in 60.29 to 60.49 seconds. Their final 2,048-byte
saves were identical. The following averages use complete 120-presentation
intervals after the first interval, which includes startup work:

| Output | VSync | Presentations/s | Game steps/s | Raster ms | Present ms |
| --- | --- | ---: | ---: | ---: | ---: |
| Window, 1194 x 672 | On | 59.81 | 59.97 | 7.04 | 8.24 |
| Fullscreen, 1920 x 1080 | On | 59.96 | 60.09 | 7.50 | 8.03 |
| Window, 1194 x 672 | Off | 60.10 | 60.10 | 6.70 | 0.13 |
| Fullscreen, 1920 x 1080 | Off | 60.10 | 60.10 | 6.48 | 0.12 |

Times are host-call wall times per presentation. They are not GPU execution
times. The scanline walk and scene/HUD extraction accounted for most active
work. Game logic averaged 0.28 to 0.36 ms, and uploads averaged 0.68 to 0.79 ms.
With VSync off, the normal frame limiter waited 8.65 to 8.95 ms per presentation.
The large drop in presentation time supports display waiting as its main cost
with VSync on. Fullscreen did not reproduce a sustained 25 FPS slowdown here.

These are single runs on an active desktop. Startup intervals included frame
times of 125 to 182 ms. The slowest later 120-presentation interval in the
windowed VSync run averaged 58.08 FPS. These results do not establish behaviour
on AMD graphics, other distributions, X11, or Wine/Proton. Linux custom race
filters were not active.

To repeat a comparison, copy the executable and its `assets/` folder into a
separate writable directory. Put a `config.ini` beside that copy:

```ini
[Settings]
Fullscreen = 0
WindowScale = 3
Widescreen = 1
VisualStyle = 0
IgnoreAspect = 0
LinearFilter = 0
ShowFPS = 0
EnableAudio = 1
AudioFreq = 48000
Volume = 0
Player1Source = 0
```

Run from that directory with the verified ROM path:

```sh
SDL_VIDEODRIVER=wayland SNESRECOMP_FRAME_TIMING=1 \
SNESRECOMP_MAX_FRAMES=3600 SNESRECOMP_VSYNC=1 \
  ./fzero_recomp "$FZERO_ROM" > timing.log 2>&1
```

Use a separate copy for each combination of `Fullscreen` (0 or 1) and
`SNESRECOMP_VSYNC` (0 or 1). Run one copy at a time. Leave screenshot and
readback diagnostics off. `Player1Source = 0` disables gameplay input;
select Keyboard or Gamepad in the launcher before a manual play test.

## Manual gameplay check

On the same machine, the owner tested the launcher, gameplay, audio, gamepad
controls, and display settings with an Xbox One S Controller. The checklist
included Original, 16:9, 32:9, fullscreen, and pause/resume through the settings
menu. The owner reported that all felt normal. Audio was enabled at 48 kHz
and 40% volume through PipeWire and the HDMI output.

Two F12 captures produced valid PNGs at 1194 x 672 and 1920 x 1080. Inspection
confirmed that they contain the game and HUD, with no settings or FPS overlay.
The game exited normally. The 2,048-byte save passed its legacy checksum and
extended-record CRC checks and contained a completed race and best-lap time.
The settings file retained the selected fullscreen, aspect, scale, stretch,
FPS display, audio, and gamepad values.

After a relaunch, the owner confirmed that the settings and both record times
appeared correctly in-game. The second run exited normally. The save,
settings, and keyboard bindings were byte-for-byte identical to the files
preserved after the first exit, and the save checksums remained valid.

Full keyboard gameplay remains untested. This play test used changing settings,
menu pauses, and screenshots; use the separate attract-sequence measurements
above for performance comparisons.
