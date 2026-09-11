#include "fzero_ground.h"
#include "display_layout.h"
#include "fzero_scene.h"
#include <string.h>

static unsigned Word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }

void FZeroGroundPrepare(FZeroGround *ground, const uint8_t *ram) {
    ground->ready = false;
    if (!FZeroSceneWide(ram)) return;
    unsigned world = Word(ram + 0xb0);
    if (world < 0x4c00 || world > 0x4f00 || (world & 255)) return;
    ground->world = world;
    ground->scroll_x = Word(ram + 0xa2);
    ground->scroll_y = Word(ram + 0xa6);
    memcpy(ground->map, ram + 0x10000, sizeof(ground->map));
    ground->ready = true;
}

bool FZeroGroundTile(const FZeroGround *ground, unsigned x, unsigned y,
                     uint8_t *tile) {
    if (!ground->ready) return false;
    /* The course is a wrapping grid of rooms, each containing 16x16 panels.
     * Each panel supplies four 8x8 tiles in column order. All mutable map
     * levels come from this frame's RAM, including altered track panels. */
    x &= 8191;
    y &= 4095;
    unsigned room = ground->map[ground->world + (y >> 8) * 32 + (x >> 8)];
    unsigned row = Word(ground->map + 0x5000 + room * 32 + ((y >> 4) & 15) * 2);
    unsigned entry = row + ((x >> 4) & 15) * 2;
    if (entry > 0xfffe) return false;
    unsigned panel = Word(ground->map + entry);
    if (panel > 0xfffc) return false;
    *tile = ground->map[panel + ((x >> 3) & 1) * 2 + ((y >> 3) & 1)];
    return true;
}

static int Signed13(unsigned value) {
    value &= 8191;
    return value & 4096 ? (int)value - 8192 : (int)value;
}

static int ScrollDelta(int value) {
    return value & 8192 ? (int)((unsigned)value | ~1023u) : value & 1023;
}

static void BeginPass(FZeroGround *ground) {
    if (++ground->serial == 0) {
        memset(ground->stamps, 0, sizeof(ground->stamps));
        ground->serial = 1;
    }
}

bool FZeroGroundRenderLine(FZeroGround *ground, Ppu *copy, int line) {
    /* This layout separates the Mode 7 map from OBJ graphics. Do not remap
     * shared VRAM in other layouts or change hardware overflow/mosaic rules. */
    if (!ground->ready || line < 1 || line > FZERO_DISPLAY_HEIGHT ||
        PPU_mode(copy) != 7 || PPU_m7extBg(copy) || (copy->m7sel & 0xc0) ||
        PPU_mosaicEnabled(copy, 0) || (copy->obsel & 31) != 2 ||
        copy->extraLeftRight != FZERO_WIDE_MARGIN) return false;

    int cx = Signed13(copy->m7matrix[4]), cy = Signed13(copy->m7matrix[5]);
    int dh = ScrollDelta(Signed13(copy->m7matrix[6]) - cx);
    int dv = ScrollDelta(Signed13(copy->m7matrix[7]) - cy);
    int y = PPU_m7yFlip(copy) ? 255 - line : line;
    /* Keep the hardware's fixed-point truncation before adding the screen X
     * term. The full course offset is an integral number of cache periods. */
    uint32_t sx = (copy->m7matrix[0] * dh & ~63) +
                  (copy->m7matrix[1] * y & ~63) +
                  (copy->m7matrix[1] * dv & ~63) + cx * 256;
    uint32_t sy = (copy->m7matrix[2] * dh & ~63) +
                  (copy->m7matrix[3] * y & ~63) +
                  (copy->m7matrix[3] * dv & ~63) + cy * 256;
    unsigned ox = ground->scroll_x + 512u - cx;
    unsigned oy = ground->scroll_y + 512u - cy;
    /* The cache origin can cross a boundary between frames. Only accept the
     * metadata paired with these raster registers, never a newer camera. */
    if ((ox | oy) & 1023) return false;

    enum { count = FZERO_WIDE_MARGIN * 2 };
    uint16_t addresses[count];
    uint8_t tiles[count];
    uint32_t pixels[count];
    unsigned corrected = 0;
    for (int i = 0; i < count; ++i) {
        int x = i < FZERO_WIDE_MARGIN ? i - FZERO_WIDE_MARGIN :
                                                       i - FZERO_WIDE_MARGIN + 256;
        int rx = PPU_m7xFlip(copy) ? 255 - x : x;
        unsigned u = (sx + (uint32_t)(copy->m7matrix[0] * rx)) >> 8;
        unsigned v = (sy + (uint32_t)(copy->m7matrix[2] * rx)) >> 8;
        addresses[i] = ((v >> 3) & 127) * 128 + ((u >> 3) & 127);
        if (!FZeroGroundTile(ground, u + ox, v + oy, &tiles[i])) return false;
        corrected += (copy->vram[addresses[i]] & 255) != tiles[i];
    }

    uint32_t *output = (uint32_t *)(copy->renderBuffer + (line - 1) * copy->renderPitch);
    int first = 0;
    BeginPass(ground);
    for (int i = 0; i <= count; ++i) {
        /* Two course positions can alias the same cache cell on one line.
         * Finish the current span before assigning that cell a different tile.
         * Each pass still uses the PPU's own sprites, windows and colour math. */
        bool conflict = i < count && ground->stamps[addresses[i]] == ground->serial &&
                        (copy->vram[addresses[i]] & 255) != tiles[i];
        if (i == count || conflict) {
            ppu_runLine(copy, line);
            for (int j = first; j < i; ++j)
                pixels[j] = output[j < FZERO_WIDE_MARGIN ? j : j + 256];
            if (i == count) break;
            ++ground->split_passes;
            first = i;
            BeginPass(ground);
        }
        unsigned address = addresses[i];
        ground->stamps[address] = ground->serial;
        /* High bytes hold the current animated tile artwork. */
        copy->vram[address] = (copy->vram[address] & 0xff00) | tiles[i];
    }
    for (int i = 0; i < count; ++i)
        output[i < FZERO_WIDE_MARGIN ? i : i + 256] = pixels[i];
    ++ground->lines;
    ground->corrected_pixels += corrected;
    return true;
}
