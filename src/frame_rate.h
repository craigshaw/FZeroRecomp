#pragma once

#include <stdint.h>

/* Sample completed presentations over at least half a second. Timing spans
 * rendering, vsync, and host pacing. A negative rate means no sample yet. */
typedef struct FZeroFrameRate {
    uint64_t start_ns;
    unsigned frames;
    double fps;
} FZeroFrameRate;

static inline void FZeroFrameRateInit(FZeroFrameRate *rate, uint64_t now_ns) {
    rate->start_ns = now_ns;
    rate->frames = 0;
    rate->fps = -1.0;
}

static inline void FZeroFrameRatePresent(FZeroFrameRate *rate, uint64_t now_ns) {
    ++rate->frames;
    uint64_t elapsed = now_ns - rate->start_ns;
    if (elapsed >= 500000000) {
        rate->fps = rate->frames * 1e9 / (double)elapsed;
        rate->frames = 0;
        rate->start_ns = now_ns;
    }
}
