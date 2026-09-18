#pragma once
#include "fzero_records_runtime.h"
#include "snes/ppu.h"

typedef struct FZeroRecordsView {
    Ppu scratch;
    bool active;
} FZeroRecordsView;
/* Rebuild only a copied records-page PPU. Its scanlines replace the native
 * output before normal scene/HUD capture, scaling, filters and screenshots. */
void FZeroRecordsViewLine(FZeroRecordsView *view, const FZeroRecordsRuntime *records,
                          const Ppu *ppu, int line);
/* Original-tile header extensions for all 15 Records tracks. Render into a
 * full-height wide scratch surface; the caller uses only the side columns. */
bool FZeroRecordsBackdropLine(FZeroRecordsView *view, const FZeroRecordsRuntime *records,
                              const Ppu *ppu, int line, uint8_t *pixels, size_t pitch);
