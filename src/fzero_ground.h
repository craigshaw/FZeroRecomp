#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "snes/ppu.h"

typedef struct FZeroGround {
    /* A frame-owned copy keeps course edits paired with the pending upload. */
    uint8_t map[0x10000];
    uint16_t world, scroll_x, scroll_y;
    bool ready;
    uint32_t stamps[0x4000], serial;
    unsigned long lines, corrected_pixels, split_passes;
} FZeroGround;

void FZeroGroundPrepare(FZeroGround *ground, const uint8_t *ram);
bool FZeroGroundTile(const FZeroGround *ground, unsigned x, unsigned y,
                     uint8_t *tile);
/* Draw only on the disposable wide PPU. False requests the normal fallback. */
bool FZeroGroundRenderLine(FZeroGround *ground, Ppu *copy, int line);
