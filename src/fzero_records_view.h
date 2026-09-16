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
