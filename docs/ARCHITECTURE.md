# Architecture

The ROM and reviewed cfg feed snesrecomp's analyser, which emits C for proven
`(pc24, M, X)` execution variants. Unresolved execution uses the shared 65816
interpreter. The F-Zero host schedules execution and presents the shared SNES
hardware output through SDL3.

## Ownership

| Component | Responsibility |
| --- | --- |
| `src/main.c`, `src/config.c` | ROM verification, paths, audio, input, settings, and the desktop loop |
| `src/fzero_rtl.c`, `src/fzero_spc_player.c` | Game scheduling and audio integration |
| `src/launcher_settings.*`, `src/launcher_controls.inc` | Launcher settings transfer and F-Zero menu controls |
| `src/runtime_ui*` | Settings adapter, paused menu, and FPS overlay |
| `src/presentation.c` | Scene/HUD composition, shaders, and scaling |
| `src/fzero_layers.c`, `src/fzero_scene.h` | Display-state eligibility, panorama extension, HUD capture and placement |
| `src/fzero_ground.c`, `src/fzero_vehicles.c` | Course sampling and vehicle reconstruction for the wider view |
| `snesrecomp/` | Analysis, C emission, interpreter, and SNES devices |
| `recomp-ui/` | Launcher and runtime UI, pinned at `773155ae7d3be80a21d40851b58f99c79e003de1` |

Generic dependency changes belong in snesrecomp. The reviewed integration and
recovery process are in [SNESRECOMP_PATCHES.md](SNESRECOMP_PATCHES.md).

## Frame scheduling

Reset/mainline execution uses the cooperative interpreter bridge. The host
calls the configured NMI and IRQ entries and preserves their hardware stack
model. Each scanline is drawn and captured before the next HDMA/IRQ update;
a handler's register changes affect the following row.

During this scanline walk, the host calls
`snes_set_hdma_beam_enabled(g_snes, false)` so the dependency's beam simulator
does not run a second HDMA engine. It saves the prior per-instance setting and
restores it after the walk, including when beam HDMA was already disabled.
Preserve this ordering when changing the scheduler.

Course, vehicle, and scene-policy snapshots are taken before NMI with the
matching display upload. Reading them after the next guest update can combine
new positions or menu state with an older frame.

## Display composition

The host retains native 256x224 and wide 398x224 scene/HUD surfaces. The wide
view adds 71 columns on each side. Both views update each frame, so an aspect
or style switch while paused needs no new game frame.

Side rendering operates on copied PPU state. The native centre is inserted
before HUD relocation. Live PPU state, guest memory, physics, object lifetime,
and native sprite limits are unchanged.

- **Ground:** resolve the frame's full course map instead of wrapping the
  moving VRAM cache. Check camera/raster alignment and render separate spans
  when margin samples conflict at one cache address.
- **Scenery:** unwrap recognised sky and horizon panorama strips, retaining
  each layer's scroll and repeat period.
- **Vehicles:** project active cars with ROM perspective/layout data and current
  ROM/RAM graphics. Keep native pieces when available and preserve depth,
  upload timing, shadow cadence, and conservative exceptional-state guards.
- **HUD:** capture final visible RGB, including brightness, windows, and colour
  math. Restore the scene beneath old instrument positions before placing the
  instruments at the wider edges. Keep messages and repair sprites centred;
  exclude slide sparks from HUD capture. Preserve opaque black power-bar fill.
  Treat sprite slots 126 and 127 as shadows in racing uploads; their counter
  role applies only to full native uploads.

| Layout | Wide view | Colour and HUD policy |
| --- | --- | --- |
| Racing, pause, recharge, recovery | Extended course and supported vehicles | Selected style; protected HUD; instruments at wider edges |
| Course intro and countdown | Extended course and backdrop | Selected style from the first visible intro frame; protected intro lettering, then racing HUD |
| Recognised crash, finish, and loss | Wide while the racing layout remains valid | Instruments stay wide; full native effects retain Original colours |
| Race results and GP ending camera | Extended backdrop | Selected style; protected result lettering; text stays centred |
| Title | Extended backdrop | Original colours; title text stays centred |
| Unrecognised layout or unsupported render mode | Native view | Original colours |

Object creation/removal and depth limits remain those of the game, so cars may
still appear or disappear at native simulation boundaries. Course and exceptional
animation coverage is incomplete. Widescreen adds no HD Mode 7 or interpolation.

## Shaders and UI

With SDL 3.4+, the GPU renderer runs a Metal scene shader on macOS or an
embedded DXIL scene shader on Windows, then draws the protected HUD with its
original RGB. Original preserves colours; Enhanced
adds colour grading and bloom; Vivid adds saturation and contrast; Black & White
uses weighted greyscale. Other renderers use SDL composition with effects disabled.
The saved style remains available for a later supported run.

The Windows HLSL equivalent lives in `src/shaders/scene.hlsl`; CMake uses the
Windows SDK's DXC to compile and embed it in both the game and presentation
tests. Keep its equations and style values aligned with the Metal source in
`src/presentation.c`. SDL's shipped DXIL vertex shader exposes colour as
`TEXCOORD0` and UV as `TEXCOORD1` (the original HLSL names are remapped by SDL's
shader build). A resource-compatible shader can still fail at pipeline creation
if these semantics do not match, so actual GPU readback is required.

GPU tests use visible windows and present after a synchronised resize before
reading the window image. SDL's GPU backbuffer adopts the swapchain dimensions
on presentation; reading new window dimensions against the old backbuffer can
fail inside the graphics driver. Scene/HUD readback uses fixed-size textures.

The runtime menu and FPS readout draw after game composition. Restore the game
viewport and logical presentation after ImGui. The local SDL3 renderer backend
is covered by [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Opening settings pauses simulation and audio. Menu input is withheld from the
game; held navigation buttons are suppressed until released. The launcher and
runtime menu share settings. Source builds use executable-relative storage;
the packaged Mac app uses `~/Library/Application Support/FZeroRecomp/` and
loads assets from its bundle. Save migration in source builds copies a legacy
save only when the destination is absent and retains the original.

The launcher's Display menu uses the same seven controls and order as the
in-game menu. `launcher_settings.c` transfers standard ABI fields and stages
Race Filter and FPS Readout, which the pinned launcher ABI does not contain.
Play accepts those staged settings; closing the launcher discards them. The
hotkey panel lists fixed Settings, Screenshot and Quit shortcuts plus the
existing FPS binding editor. The controller page offers the input sources and
keyboard bindings the host reads, with fixed gamepad mapping explained.

`cmake/fzero_launcher.cmake` applies the host adapter in `patches/recomp-ui/`
to a build-local copy of the pinned ImGui backend. The adapter selects the
host's control renderers and grows the Display card. The dependency checkout
and ABI stay unchanged. A patch mismatch stops configuration and requires
review when updating recomp-ui. See `patches/recomp-ui/README.md`.


## Diagnostics

Run with these environment variables as needed; keep captures and logs private.
Use isolated executable-relative settings and saves for comparisons.

| Variable | Purpose |
| --- | --- |
| `SNESRECOMP_VALIDATE_PRESENTATION=1` | GPU readback comparison; requires the shader path and exits on a mismatch |
| `SNESRECOMP_HUD_DIAGNOSTIC=1` | Greyscale scene with coloured protected pixels; requires the shader path |
| `SNESRECOMP_PRESENTATION=legacy` | Compare with the older single-texture native presentation |

Readback checks all composite RGB in Original and protected HUD RGB in effect
styles. It checks composition, not whether every HUD item was correctly classified.
Use synthetic tests and interactive inspection for that distinction.
