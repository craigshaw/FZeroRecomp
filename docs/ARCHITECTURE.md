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
| `src/fzero_records*`, `src/fzero_save.*` | Per-car records, compatible SRAM extension, Records presentation, and safe save replacement |
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

### Desktop pacing

The desktop host uses a monotonic game clock at approximately 60.098812 Hz,
matching the NTSC frame rate. This replaces the 60.000 Hz desktop limiter and
keeps game speed independent of display refresh. `src/game_clock.h` schedules
steps; `src/main.c` calls the runner and requests VSync for presentation.
The displayed FPS readout measures presentations, not simulation steps.

Each due step executes game logic, the records observer, and the full scanline
walk. Only the last image is uploaded when several steps are due together.
On a 60 Hz display this normally means an extra game step about every ten
seconds. Higher refresh rates can repeat an image without advancing the game.
The host sleeps only for time not already spent in game work and presentation.

Catch-up is limited to three steps. Longer stalls restart the clock without
accumulating a large backlog. Opening settings clears elapsed time and pending
input, and pauses audio. Resuming starts one immediate step. Short button
presses are retained until the next game step; additional steps in the same
host iteration use the current held controls. Changing input source or losing
focus clears retained input. The existing audio consumer adjusts its sample
rate for small differences between the game clock and the audio device.

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
  role applies only to full native uploads. During the GP ending transition,
  keep the map, markers, lives and boost indicators at the wide edges. Below
  the top band, check both sprite slot and corner position because the results
  table later reuses the same slots for centred lettering. Crash animation
  retains HUD slots 0..46 and reuses slots 48 onward for explosion and smoke.
  This ownership starts at explosion counter 7, before the full native upload
  at counter 19. Keep those pieces filtered and at their live positions from
  the first explosion phase; the normal tail-counter rule does not apply.
  Intro and standalone results uploads can use slots 126..127 for lives.
  Recognise them only in the lower-right counter area: practice track selection
  reuses slots 120..127 for its map, which must remain together in that layout.
  Move recognised lives counters to the right edge and the results score in the upper-left
  BG3 band to the left edge. Keep other lettering centred, including text
  uncovered at an old counter position. These layouts have no racing power mask.

| Layout | Wide view | Colour and HUD policy |
| --- | --- | --- |
| Racing, pause, recharge, recovery | Extended course and supported vehicles | Selected style; protected HUD; instruments at wider edges |
| Course intro and countdown | Extended course and backdrop | Selected style; lives at the wide right edge; centred intro lettering, then racing HUD |
| Crash explosion and loss animation | Extended course while its layout remains valid | Selected style through flashes and fades; protected HUD and centred message |
| Recognised finish and other loss | Wide while the racing layout remains valid | Instruments stay wide; unclassified full native effects retain Original colours |
| Race results and crashed-out page | Extended backdrop | Selected style; score and lives at wide edges; protected result lettering stays centred |
| GP ending camera | Extended backdrop and racing vehicles | Selected style; corner instruments at wider edges; protected results table stays centred |
| Title | Extended backdrop | Original colours; title text stays centred |
| Records: all 15 tracks | Tile-based header extensions; centred body | Original colours, including fades |
| Unrecognised layout or unsupported render mode | Native view | Original colours |

Object creation/removal and depth limits remain those of the game, so cars may
still appear or disappear at native simulation boundaries. Course and exceptional
animation coverage is incomplete. Widescreen adds no HD Mode 7 or interpolation.

## Records and saves

The records codec owns only the final 1,536 bytes of the 2 KB SRAM file. The
first 512 bytes retain their existing layout and remain under guest control.
Each track/car stores ten race times and five lap times using exact sorted-list
ranks. The versioned extension has its own CRC and a separate legacy-block
CRC for detecting external changes. See [RECORDS_EXTENSION.md](RECORDS_EXTENSION.md).

The desktop loop takes a records snapshot before game execution and observes
completion after it. Only a player race that finishes all five laps supplies
candidates. The host computes the fastest lap from the five cumulative times
and processes the event once. Later guest record updates also trigger saving;
they do not import laps from incomplete races during the session.

Transient highlights retain the inserted car-list positions for each track's
latest race and lap candidates. This identifies new entries even when times
tie. The mixed view maps those positions through the same stable car merge.
It replaces the guest's legacy marker sprites in the copied PPU, keeps their
palette animation, and uses separate slots from row car icons. No highlight
metadata is stored in SRAM. A retry clears that track's highlights; a new
selection, title reset, or save reload clears the session's highlights.

Records presentation uses a copied PPU below the backdrop, before scene/HUD
capture. It keeps the loaded lettering, car art, track name and map, aligns
the race column, and adds the lap column. Guest PPU state and memory are not
changed. A fifth mixed page selects the top ten race and top five lap times
across the four stored lists, preserving ties and source-car identity without
extra save data. Mixed rows use that identity for their car icon. Four car
tabs keep fixed positions with a one-pixel gap; inactive cars use greyed car
palettes only in the copied PPU. Title, map and text palettes remain intact.
Left/right is intercepted on Records pages, with one change per press.
Both menu and post-GP entry select the raced car. Page metadata remains
active through the exit fade, until the next menu loads, so the original
layout cannot appear during the transition. The guest retains track
navigation, exit and confirmation handling.
The host observes an accepted clear and clears every car on that track.

The Records headers have separate tilemaps from the racing panoramas. The
wide view covers all 15 tracks with eight shared tile arrangements, retaining
each track's resident palette. It adds nine source tile columns per side,
cropped to the existing 71-pixel margins. Fixed column recipes continue the
low skyline, terrain and sky, preserving the central landmarks. Port Town
continues its horizontal sky edge tiles above the distant structures, so its
sloping bands do not restart in the margins. The
renderer copies the current PPU state, uses resident tile attributes and
palettes, and draws only BG1 and BG2 into a wide scratch surface. Composition
copies only the side columns into the protected colour layer. Native output
and the Records body remain unchanged. Page and resident-art guards reject
unsupported layouts and mismatched track uploads.

The host save module loads SRAM, preserves damaged input in a numbered recovery
file, and disables saving if recovery cannot be retained or the extension is
newer than this host supports. Writes stage a complete temporary file, retain
a previous valid backup, and replace the save only after closing the temporary
file successfully. Write failures leave the current save in place and disable
further attempts for that session. The launcher still imports or clears the
whole file through its existing backup path.

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
