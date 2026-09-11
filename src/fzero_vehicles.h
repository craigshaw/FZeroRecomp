#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "snes/ppu.h"

/* Presentation data belongs to the frame about to be uploaded by vblank.
 * No pointers into mutable guest memory survive preparation. */
typedef struct FZeroVehicle {
    bool visible, added;
    int x, ground_y;
    unsigned size, count;
    uint16_t oam[16], high;
    uint16_t graphics[512];
    unsigned graphics_address;
} FZeroVehicle;

typedef struct FZeroVehicles {
    bool ready;
    FZeroVehicle car[6];
    uint16_t shadow_oam[24];
    uint8_t shadow_high[3];
    unsigned shadow_count;
    int jump_anchor_y[6];
    bool jump_anchor_valid[6];
    unsigned long projected, matched, added_left, added_right;
    unsigned long jump_reprojected;
} FZeroVehicles;

/* Zero-initialise the frame before its first use. RAM is the 128 KiB WRAM view.
 * Perspective tables and artwork are read from the supplied cartridge.
 * Test callers supply invented memory and tables. */
bool FZeroVehicleProject(int16_t lateral, int16_t depth, const uint8_t *rows,
                          const uint8_t *scales, int *x, int *y);
void FZeroVehiclesPrepare(FZeroVehicles *frame, const uint8_t *ram,
                           const uint8_t *rom, size_t rom_size);
/* Apply only to the disposable wide PPU. The authentic centre is inserted
 * after this pass, and live OAM, VRAM and gameplay remain untouched. */
void FZeroVehiclesApply(const FZeroVehicles *frame, Ppu *copy);
