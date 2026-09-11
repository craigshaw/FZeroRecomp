#pragma once

#include "common_cpu_infra.h"

/* F-Zero (US) frame driver. Mirrors the SMW game layer: the host main loop
 * calls RtlRunFrame() which invokes FZeroRunOneFrameOfGame(), then the host
 * calls FZeroDrawPpuFrame() to render the PPU field (HDMA + raster IRQ). */
void FZeroRunOneFrameOfGame(void);
void FZeroDrawPpuFrame(void);

struct FZeroLayers;
void FZeroSetLayers(struct FZeroLayers *layers);

extern const RtlGameInfo kFZeroGameInfo;
