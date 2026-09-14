#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "snes/ppu.h"
#include "display_layout.h"
#include "fzero_vehicles.h"
#include "fzero_ground.h"

enum { FZERO_LAYER_WIDTH = FZERO_NATIVE_WIDTH, FZERO_LAYER_HEIGHT = FZERO_DISPLAY_HEIGHT };
typedef struct FZeroLayers {
    uint32_t world[FZERO_LAYER_HEIGHT][FZERO_LAYER_WIDTH];
    uint32_t hud[FZERO_LAYER_HEIGHT][FZERO_LAYER_WIDTH];
    uint32_t capture[FZERO_LAYER_HEIGHT][FZERO_LAYER_WIDTH];
    uint32_t wide_world[FZERO_LAYER_HEIGHT][FZERO_WIDE_WIDTH];
    uint32_t wide_hud[FZERO_LAYER_HEIGHT][FZERO_WIDE_WIDTH];
    uint32_t wide_capture[FZERO_LAYER_HEIGHT][FZERO_WIDE_WIDTH];
    Ppu scratch;
    FZeroVehicles vehicles;
    FZeroGround ground;
    /* Policy for the pending upload, captured with vehicles and ground. */
    bool wide_scene, hud_layout, native_oam, intro_panorama, results_layout;
    /* Explosion phases reuse rank slots before the full native upload begins. */
    bool crash_layout;
    /* Instrument placement is independent of selective scene effects. */
    bool move_hud;
    unsigned long wide_lines;
    unsigned long extracted_lines, protected_lines, hud_pixels;
} FZeroLayers;

/* Called after the authentic scanline and before HDMA/IRQ changes its state.
 * Only the copied PPU is redrawn. Live PPU and guest memory remain untouched.
 * racing enables background and vehicle-edge expansion. hud_layout permits racing HUD
 * extraction; intro_panorama and results_layout protect lettering while filtering scenery.
 * Other layouts retain Original colours across the full width. */
void FZeroLayersProcessLine(FZeroLayers *layers, const Ppu *ppu, int line,
                            bool racing, bool hud_layout);
