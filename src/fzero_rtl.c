/*
 * F-Zero (US) game-layer frame driver.
 *
 * Frame model (see snesrecomp/docs/FRAME_MODEL_HOSTS.md):
 *
 *   Reset at $00:8000 initializes the machine and enters the cooperative
 *   main loop. Each iteration establishes 8-bit registers, clears the
 *   software NMI flag at WRAM $7E:0040, and waits at $00:803A for vblank.
 *
 *   NMI ($00:80D9) runs first each vblank, does all per-frame PPU writes, then
 *   sets the software NMI flag to 0xFF. The main loop then
 *   runs one mode dispatch and loops back to the spin.
 *
 *   This is the exact "cooperative scheduler" shape interp_bridge's
 *   scheduler helper models: we enter it at the spin PC ($00:803A), yield
 *   when it reaches the spin with the flag cleared (one frame's dispatch
 *   complete). The host drives NMI delivery; the main loop is LLE'd through
 *   the bridge so every AOT'd task body bounces to compiled code.
 *
 *   The reset entry has no AOT body in the current manifest (it is one of the
 *   LLE-only `unproven_callee_exit` nodes), so the reset path itself runs
 *   through the same scheduler bridge: entering at $00:8000 runs boot init,
 *   falls into the loop, and yields at the spin once the flag is cleared.
 */
#include "fzero_rtl.h"
#include "fzero_layers.h"
#include "fzero_scene.h"
#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "snes/snes.h"
#include "snes/ppu.h"
#include "snes/interp_bridge.h"
#include "cpu_state.h"
#include "funcs.h"

/* Reset vector: $00:8000. */
static const uint32 kFZeroResetPc = 0x008000u;
/* Main loop waits here for the software NMI flag. */
static const uint32 kFZeroLoopSpinPc = 0x00803Au;
/* Software NMI flag in WRAM, with direct page zero. */
static const uint16 kFZeroNmiFlagAddr = 0x0040u;
static FZeroLayers *g_layers;

void FZeroSetLayers(FZeroLayers *layers) { g_layers = layers; }

void FZeroRunOneFrameOfGame(void) {
  // First-call reset gate (host-side bool, independent of WRAM contents).
  static bool g_did_reset = false;
  static bool g_first_frame_done = false;

  if (!g_did_reset) {
    cpu_state_init(&g_cpu, g_ram);
    /* Run boot through the scheduler bridge: reset falls into the
     * main loop and reaches the spin with the NMI flag cleared. */
    interp_bridge_run_scheduler(&g_cpu, kFZeroResetPc, kFZeroLoopSpinPc,
                                kFZeroNmiFlagAddr);
    g_did_reset = true;
  }

  /* Frame 0 is special: on real hardware the first NMI fires AFTER reset
   * completes and the main loop has spun up (flag cleared, 8-bit registers).
   * Skipping NMI on frame 0 lets the handler save the main loop's P state.
   * The reset run above already reached the spin,
   * so frame 0 yields there with the flag still cleared. */
  if (g_first_frame_done) {
    /* Pair presentation metadata with the OAM/graphics upload about to run.
     * The following game update produces the next frame's guest buffers. */
    if (g_layers) {
      g_layers->wide_scene = FZeroSceneWide(g_ram);
      g_layers->native_oam = g_ram[0x50] == 0;
      g_layers->intro_panorama = FZeroSceneIntro(g_ram);
      g_layers->hud_layout = g_layers->wide_scene && !g_layers->native_oam &&
          !g_layers->intro_panorama && !FZeroSceneTitle(g_ram) &&
          !FZeroSceneResults(g_ram) && g_ram[0xc3] != 0x11;
      /* Finish/loss cameras retain the instruments. The explosion changes
       * upload mode but keeps those slots too; its full-width colour
       * protection is independent of where the HUD is placed. */
      g_layers->move_hud = g_layers->hud_layout ||
          (g_layers->wide_scene && g_ram[0x54] == 2 && g_ram[0xc3] == 0x40);
      FZeroVehiclesPrepare(&g_layers->vehicles, g_ram, g_rom, 0x80000);
    }
    if (g_layers) FZeroGroundPrepare(&g_layers->ground, g_ram);
    /* NMI handler runs BEFORE the main-loop game code each frame. Assert the
     * hardware NMI latch so the recompiled handler's read of $4210 (RDNMI)
     * returns bit 7 = 1, matching real hardware; the read clears the latch. */
    g_snes->inNmi = true;
    /* Model the hardware NMI-entry push so the handler returns through a real
     * interrupt frame instead of over-popping the guest stack. */
    cpu_push_interrupt_frame(&g_cpu);
    bank_00_80D9(&g_cpu);
    /* Main loop: yield when it reaches the vblank-wait spin with the flag
     * cleared (one mode dispatch complete). */
    interp_bridge_run_scheduler(&g_cpu, kFZeroLoopSpinPc, kFZeroLoopSpinPc,
                                kFZeroNmiFlagAddr);
  }
  g_first_frame_done = true;
}

void FZeroDrawPpuFrame(void) {
  SimpleHdma hdma_chans[8];

  Dma *dma = g_dma;

  /* This host walks per-line HDMA itself (SimpleHdma below). Suppress the
   * beam simulator's own HDMA engine while rendering: its walk is positioned
   * by the beam (which is not at line 0 here; it is wherever the game logic
   * left it), so a dma_doHdma firing inside a raster IRQ would write the
   * window/scroll registers with the wrong band and persist through the next
   * host band hold, causing horizon-band flicker. */
  g_host_owns_hdma = 1;

  /* Re-arm HDMA channels from the $420C latch (the runner tracks it in
   * g_snesrecomp_last_hdmaen on every $420C write, including the NMI upload). */
  dma_startDma(dma, g_snesrecomp_last_hdmaen, true);

  /* F-Zero uses HDMA channels 1-7 (Mode 7 matrix/scroll splits); inactive
   * channels are no-ops (table == NULL). */
  for (int i = 0; i < 8; i++)
    SimpleHdma_Init(&hdma_chans[i], &dma->channel[i]);

  /* F-Zero arms the H/V timer IRQ ($4200 = $B1, V count $4209 = $12 = 18)
   * and chains four raster splits per frame at lines 18/28/47/86, each
   * handler re-arming the V count for the next band. The PPU line argument
   * is already the hardware scanline (line 0 is the pre-render line). Apply
   * the HBlank handler after drawing vTimer, so its writes and that line's
   * HDMA agree on the following scanline. Adding one leaves a row in the old
   * mode after the new band's scroll/matrix data has arrived.
   * The runner's vTimer is the target line, and
   * the IRQ handler reads $4211 (HW_TIMEUP), which returns inIrq<<7 and
   * clears the latch. Assert inIrq to take the timer path. */
  int trigger = g_snes->vIrqEnabled ? g_snes->vTimer : -1;

  for (int i = 0; i <= 224; i++) {
    ppu_runLine(g_ppu, i);
    /* Capture before HDMA/IRQ changes the raster state. Recharge reuses HUD
     * slots, whose visible pixels are already protected by sprite capture.
     * Keep scene effects active while the repair animation enters and leaves;
     * its lifetime must not switch the whole screen to Original colours. */
    if (g_layers) {
      /* Use the policy paired with this upload, not the next guest update's
       * menu or exception state. That update can already describe a new frame. */
      FZeroLayersProcessLine(g_layers, g_ppu, i,
                            g_layers->wide_scene, g_layers->hud_layout);
    }
    for (int c = 0; c < 8; c++)
      SimpleHdma_DoLine(&hdma_chans[c]);
    if (i == trigger) {
      g_snes->inIrq = true;
      cpu_push_interrupt_frame(&g_cpu);
      bank_00_8601(&g_cpu);
      trigger = g_snes->vIrqEnabled ? g_snes->vTimer : -1;
    }
  }

  g_host_owns_hdma = 0;
}

const RtlGameInfo kFZeroGameInfo = {
  .title = "fzero",
  .initialize = NULL,
  .run_frame = &FZeroRunOneFrameOfGame,
  .draw_ppu_frame = &FZeroDrawPpuFrame,
  /* 2 KB battery SRAM (cart header: ROM+RAM+battery, SRAM size 2 KB). The
   * runner maps it from the header automatically; RtlReadSram/RtlWriteSram
   * persist it to saves/save.srm. */
  .save_name_prefix = "save",
  .tier2_capture = 0,
};
