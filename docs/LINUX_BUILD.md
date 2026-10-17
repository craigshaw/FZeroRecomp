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

Linux builds now use SDL's Vulkan GPU renderer for Original, Enhanced, Vivid,
and Black & White, with protected HUD colours in Original, 16:9, and 32:9.
The build embeds SPIR-V compiled from the same HLSL source as Windows.
If the GPU or shader setup fails, the host logs the reason and uses SDL
composition with Original colours. The saved filter choice is retained.

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

## Install the shader compiler

The default Linux build requires DXC with SPIR-V support. The tested x86_64
compiler is Microsoft's [DXC 1.9.2609 release](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609).
Install it locally with its libraries and verify the published archive digest:

```sh
mkdir -p .tools/downloads .tools/dxc-1.9.2609
curl --fail --location --retry 3 \
  --output .tools/downloads/linux_dxc_2026_09_28.x86_x64.tar.gz \
  https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/linux_dxc_2026_09_28.x86_x64.tar.gz
printf '%s\n' \
  '96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1  .tools/downloads/linux_dxc_2026_09_28.x86_x64.tar.gz' \
  | sha256sum --check -
```

Continue only if the checksum reports `OK`:

```sh
tar -xzf .tools/downloads/linux_dxc_2026_09_28.x86_x64.tar.gz \
  -C .tools/dxc-1.9.2609
.tools/dxc-1.9.2609/bin/dxc --version
```

CMake compiles `src/shaders/scene.hlsl` to SPIR-V 1.0 and generates an embedded
header under `build/shaders/`. The game needs neither DXC nor a loose shader
file at runtime. Keep these generated files in the build directory.

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
PATH="$PWD/.tools/dxc-1.9.2609/bin:$PATH" \
  CMAKE_PREFIX_PATH="$PWD/.tools/sdl3-3.4.16" sh build.sh
ctest --test-dir build --output-on-failure
ldd build/fzero_recomp
./build/fzero_recomp "$FZERO_ROM"
```

Generation produces 678 exact AOT variants and 9 interpreter variants at the
current dependency pin. `ldd` must resolve `libSDL3.so.0` to the local `.tools`
installation and show no missing libraries. CMake records the SDL and DXC
paths in `build/CMakeCache.txt`; later source edits can use `sh build.sh`
directly. You can also set DXC explicitly with `-DFZERO_DXC=/path/to/dxc`.
If the project is moved, reconfigure it with the new SDL and DXC paths.

On a configured checkout, a build without Vulkan filters can use:

```sh
cmake -S . -B build -DFZERO_LINUX_VULKAN=OFF
sh build.sh
```

This build needs no DXC and retains Original colours and all three aspect
modes. Set `FZERO_LINUX_VULKAN=ON` to restore Vulkan filters.

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

Before Vulkan filters were added, four runs on 17 October 2026 of the same
3,600-frame attract sequence used
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

To repeat the OpenGL baseline, build with `FZERO_LINUX_VULKAN=OFF`.
With the default build, the same method measures Vulkan; check the renderer
and GPU backend in the log. Copy the executable and its `assets/` folder into a
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

This manual check preceded Vulkan filter support and used Original colours
with OpenGL.

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

## Vulkan validation

The synthetic GPU test uses real Vulkan rendering and readback. It checks
fixed RGB references for all four styles, exact protected HUD colours,
switches between all three aspect modes, nearest and linear sampling,
window resizing, overlay state restoration, screenshots, and bloom bounds.
Run it from an isolated test directory with the desktop session available:

```sh
mkdir -p build/linux-gpu-test
(cd build/linux-gpu-test && \
  SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy FZERO_TEST_GPU=1 \
  ../fzero_host_ui_tests)
```

The log must report `renderer=gpu`, `GPU backend: vulkan`, and
`host UI tests: passed`. The ordinary CTest run uses SDL's dummy video driver
and does not replace this GPU check.

To test runtime fallback with the same binary, use a deliberately unavailable
GPU driver name and require the fallback in the test:

```sh
mkdir -p build/linux-fallback-test
(cd build/linux-fallback-test && \
  SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy \
  SDL_GPU_DRIVER=fzero-unavailable-test FZERO_TEST_FALLBACK=1 \
  ../fzero_host_ui_tests)
```

This check must report the unavailable GPU driver, use SDL composition without
a custom shader, and pass. These commands were verified on the Intel GPU above.
Windows DXIL compilation was also checked with the shared HLSL. Windows and
macOS runtime tests require their own machines.

Four ROM-based Vulkan runs each completed 1,800 game frames and exited
normally. Settings and saves were isolated, audio was enabled but muted, and
input was disabled:

| Style | Aspect | Output | RGB readback result |
| --- | --- | --- | --- |
| Original | Original | Window | 1,798 full frames matched |
| Enhanced | 16:9 | Window | 1,797 protected-HUD frames matched |
| Vivid | 32:9 | Fullscreen | 1,797 protected-HUD frames matched |
| Black & White | 16:9 | Fullscreen | 1,796 protected-HUD frames matched |

A separate 3,600-frame Enhanced run at 1920 x 1080 fullscreen, 16:9, with
VSync on and readback off completed in 60.39 seconds. Complete timing intervals
after the first averaged 59.93 presentations/s and 60.10 game steps/s.
The slowest such interval averaged 59.37 FPS. The raster stage averaged
8.42 ms per presentation, upload 0.39 ms, and present 7.34 ms. These are host
wall times on this Intel machine, not GPU execution times or AMD/Wine results.

The build with `FZERO_LINUX_VULKAN=OFF` also passed the presentation tests.
There has not yet been a manual play test with Vulkan filters enabled.
