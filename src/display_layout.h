#pragma once

/* Keep the current square pixels and all 224 rows. 398 columns is the nearest
 * symmetric whole-pixel layout to 16:9 (an aspect difference below 0.06%). */
enum { FZERO_NATIVE_WIDTH = 256, FZERO_DISPLAY_HEIGHT = 224,
       FZERO_WIDE_MARGIN = 71, FZERO_WIDE_WIDTH = 398 };
static inline int FZeroDisplayWidth(int widescreen) {
    return widescreen ? FZERO_WIDE_WIDTH : FZERO_NATIVE_WIDTH;
}
