#pragma once

/* Keep the current square pixels and all 224 rows. 398 and 796 columns are the
 * nearest symmetric whole-pixel layouts to 16:9 and 32:9 (below 0.06% error). */
enum { FZERO_NATIVE_WIDTH = 256, FZERO_DISPLAY_HEIGHT = 224,
       FZERO_WIDE_MARGIN = 71, FZERO_WIDE_WIDTH = 398,
       FZERO_ULTRA_MARGIN = 270, FZERO_ULTRA_WIDTH = 796 };
static inline int FZeroDisplayWidth(int widescreen) {
    return widescreen == 2 ? FZERO_ULTRA_WIDTH :
        widescreen ? FZERO_WIDE_WIDTH : FZERO_NATIVE_WIDTH;
}

static inline int FZeroDisplayMargin(int aspect) {
    return (FZeroDisplayWidth(aspect) - FZERO_NATIVE_WIDTH) / 2;
}
