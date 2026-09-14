#include "fzero_vehicles.h"
#include "fzero_scene.h"
#include "display_layout.h"
#include <string.h>

static unsigned Word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned RomOffset(unsigned address) {
    return ((address >> 16) & 0x7f) * 0x8000 + (address & 0x7fff);
}
static const uint8_t *Rom(const uint8_t *rom, unsigned address) {
    return rom + RomOffset(address);
}

bool FZeroVehicleProject(int16_t lateral, int16_t depth, const uint8_t *rows,
                          const uint8_t *scales, int *x, int *y) {
    /* Camera depth is bounded before indexing the cartridge's inverse map.
     * Signed products truncate toward zero, matching the coordinate format. */
    int index = depth + 639;
    if (index < 0 || index >= 658) return false;
    *y = rows[index];
    *x = 128 + lateral * scales[255 - *y] / 64;
    return true;
}

static void HidePieces(uint16_t *oam, uint16_t *high) {
    /* Encoded X=256 with no coordinate hint is safely offscreen. */
    memset(oam, 0, 16 * sizeof(*oam));
    *high = 0x5555;
}

static void Piece(uint16_t *oam, uint16_t *high, unsigned piece,
                  int x, int y, bool large, unsigned attributes) {
    oam[piece * 2] = ((unsigned)y & 255) << 8 | ((unsigned)x & 255);
    oam[piece * 2 + 1] = attributes;
    unsigned shift = piece * 2;
    *high = (*high & ~(3u << shift)) |
        (((((unsigned)x & 511) >> 8) | (large ? 2 : 0)) << shift);
}

static unsigned SizeForRow(const uint8_t *rom, int y) {
    const uint8_t *limits = Rom(rom, 0x00f283);
    unsigned size = 0;
    while (size < 8 && y < limits[size]) ++size;
    return size;
}

static bool Reconstruct(FZeroVehicle *car, unsigned id, const uint8_t *ram,
                        const uint8_t *rom) {
    unsigned offset = id * 2;
    bool traffic = (ram[0xb00 + offset] & 0x40) != 0;
    int angle = (int)ram[0xbd1] - ram[0xbd1 + offset];
    if (angle < -96) angle += 192;
    if (angle >= 96) angle -= 192;
    unsigned magnitude = angle < 0 ? -angle : angle;
    if (magnitude > 96) return false;
    unsigned pose = Rom(rom, 0x00f26a)[magnitude / 4];
    car->size = SizeForRow(rom, car->ground_y);
    if (pose > 12 || car->size > 8) return false;
    unsigned entry = Rom(rom, 0x02fd56)[car->size * 13 + pose];
    if ((entry & 1) || entry > 112) return false;
    unsigned shape = entry / 2;
    bool symmetric = (Rom(rom, 0x00f330)[shape / 8] & (1u << (shape & 7))) != 0;
    unsigned layout = (traffic ? 3 : 0) + (symmetric ? 2 : angle < 0 ? 1 : 0);
    unsigned descriptor = Word(Rom(rom, 0x00faf1) + (layout * 9 + car->size) * 2);
    if (descriptor < 0x8000 || descriptor > 0xffd5) return false;
    const uint8_t *parts = Rom(rom, descriptor);
    unsigned sizes = Word(parts);
    unsigned attributes = Word(ram + 0xc40 + offset);
    int height = (int8_t)ram[0xbc1 + offset];
    int lift = height < 0 ? height :
        Rom(rom, 0x08ede0)[255 - car->ground_y] * ((height * 2) & 255) / 256;
    int y = car->ground_y - lift;
    HidePieces(car->oam, &car->high);
    car->count = 0;
    for (unsigned p = 0; p < 8; ++p) {
        const uint8_t *part = parts + 2 + p * 5;
        int dx = (int16_t)Word(part);
        if (dx == 128) break;
        Piece(car->oam, &car->high, p, car->x + dx, y + (int8_t)part[2],
              (sizes & (2u << (p * 2))) != 0, Word(part + 3) | attributes);
        ++car->count;
    }
    unsigned half_words = Rom(rom, traffic ? 0x02feec : 0x02feb3)[shape];
    unsigned source = Word(Rom(rom, traffic ? 0x02fe3f : 0x02fdcb) + entry);
    unsigned model = ram[0x1131 + offset];
    car->graphics_address = Word(Rom(rom, 0x02fd4a) + offset);
    if (!half_words || half_words > 256 || (!traffic && model > 3) ||
        car->graphics_address < 0x4000 || car->graphics_address > 0x4800)
        return false;
    const uint8_t *pixels;
    if (traffic) {
        if (source + half_words * 4 > 0x10000) return false;
        pixels = ram + source;
    } else {
        if (source < 0x8000 || source + half_words * 4 > 0x10000) return false;
        pixels = Rom(rom, ((8 + model) << 16) | source);
    }
    memset(car->graphics, 0, sizeof(car->graphics));
    for (unsigned row = 0; row < 2; ++row)
        for (unsigned w = 0; w < half_words; ++w)
            car->graphics[row * 256 + w] = Word(pixels + (row * half_words + w) * 2);
    return true;
}

static bool TouchesNativeView(const FZeroVehicle *car) {
    /* Racing OBJ pieces are 8 or 16 pixels. Unwrap each piece around its
     * signed group origin, so left-side coordinates cannot look like right. */
    for (unsigned p = 0; p < car->count; ++p) {
        unsigned high = (car->high >> (p * 2)) & 3;
        int x = (car->oam[p * 2] & 255) | ((high & 1) << 8);
        while (x - car->x > 255) x -= 512;
        while (x - car->x < -256) x += 512;
        if (x < 256 && x + (high & 2 ? 16 : 8) > 0) return true;
    }
    return false;
}

static bool ReprojectSideJump(FZeroVehicle *car, unsigned id, unsigned flags,
                              int x, int y, int anchor_y,
                              const uint8_t *ram, const uint8_t *rom) {
    /* A close airborne car can enter the native departure animation merely
     * by crossing a horizontal edge. Its native anchor then stops following
     * the ground projection. Translate the current native animation using
     * the last ground anchor and the current projection. Keep departures
     * through the near plane and any native-visible artwork under the
     * original game's control. */
    if (!(flags & 0x10) || (flags & 3) || !(ram[0xd51 + id * 2] & 0x80) ||
        (x >= -32 && x < 288) || x <= -FZERO_WIDE_MARGIN - 64 ||
        x >= 256 + FZERO_WIDE_MARGIN + 64 || TouchesNativeView(car)) return false;
    FZeroVehicle candidate = *car;
    for (unsigned p = 0; p < car->count; ++p) {
        unsigned high = (car->high >> (p * 2)) & 3;
        int px = (car->oam[p * 2] & 255) | ((high & 1) << 8);
        while (px - car->x > 255) px -= 512;
        while (px - car->x < -256) px += 512;
        Piece(candidate.oam, &candidate.high, p, px + x - car->x,
              (car->oam[p * 2] >> 8) + y - anchor_y,
              (high & 2) != 0, car->oam[p * 2 + 1]);
    }
    candidate.x = x;
    candidate.ground_y = y;
    candidate.size = SizeForRow(rom, y);
    if (TouchesNativeView(&candidate)) return false;
    *car = candidate;
    return true;
}

static void PrepareShadows(FZeroVehicles *frame, const uint8_t *ram, const uint8_t *rom) {
    frame->shadow_count = 0;
    memset(frame->shadow_oam, 0, sizeof(frame->shadow_oam));
    memset(frame->shadow_high, 0, sizeof(frame->shadow_high));
    /* Preserve the alternating shadow cadence at the native edges. */
    for (unsigned id = 1 + (ram[0x51] & 1); id < 6; id += 2) {
        const FZeroVehicle *car = &frame->car[id];
        if (!car->visible || car->size >= 8) continue;
        const uint8_t *parts = Rom(rom, 0x0becd0) + car->size * 16;
        int y = car->ground_y - Rom(rom, 0x0becc2)[car->size];
        for (unsigned p = 0; p < 4; ++p) {
            int dx = (int16_t)Word(parts + p * 4);
            if (!dx) break;
            unsigned n = frame->shadow_count++;
            int x = car->x + dx;
            unsigned flags = parts[p * 4 + 3];
            frame->shadow_oam[n * 2] = ((unsigned)y & 255) << 8 | ((unsigned)x & 255);
            frame->shadow_oam[n * 2 + 1] = parts[p * 4 + 2] | (flags >> 1) << 8;
            frame->shadow_high[n / 4] |=
                ((((unsigned)x & 511) >> 8) | ((flags & 1) << 1)) << ((n & 3) * 2);
        }
    }
}

void FZeroVehiclesPrepare(FZeroVehicles *frame, const uint8_t *ram,
                           const uint8_t *rom, size_t rom_size) {
    frame->ready = false;
    memset(frame->car, 0, sizeof(frame->car));
    /* The GP ending camera retains the racing vehicle buffers while showing
     * results. Keep reconstructing cars beyond the native horizontal edges. */
    if (!rom || rom_size != 0x80000 || !FZeroSceneWide(ram) ||
        ram[0x54] != 2 || ram[0x55] < 3 ||
        (ram[0xc3] && ram[0xc3] != 0x11)) {
        memset(frame->jump_anchor_valid, 0, sizeof(frame->jump_anchor_valid));
        return;
    }
    for (unsigned id = 0; id < 6; ++id) {
        FZeroVehicle *car = &frame->car[id];
        unsigned offset = id * 2, flags = ram[0xb00 + offset];
        if (!(flags & 0x80) || (flags & 4)) {
            frame->jump_anchor_valid[id] = false;
            continue;
        }
        bool projected = id && FZeroVehicleProject((int16_t)Word(ram + 0x1170 + offset),
            (int16_t)Word(ram + 0x1180 + offset), Rom(rom, 0x09ed00),
            Rom(rom, 0x09ec00), &car->x, &car->ground_y);
        int projected_x = car->x, projected_y = car->ground_y;
        if (!(flags & 0x10)) {
            frame->jump_anchor_valid[id] = projected && (flags & 8) &&
                (ram[0xd51 + offset] & 0x80);
            frame->jump_anchor_y[id] = projected_y;
        }
        if (flags & 8) {
            if (projected && !(flags & 0x12)) {
                ++frame->projected;
                frame->matched += car->x == (int16_t)Word(ram + 0xc50 + offset) &&
                                  car->ground_y == ram[0xc60 + offset];
            }
            car->x = (int16_t)Word(ram + 0xc50 + offset);
            car->ground_y = ram[0xc60 + offset];
            car->size = ram[0xc31 + offset];
            car->count = id ? ram[0x11d0 + offset] : 8;
            if (car->count > 8) car->count = 0;
            for (unsigned w = 0; w < 16; ++w)
                car->oam[w] = Word(ram + 0x300 + id * 32 + w * 2);
            car->high = Word(ram + 0xd80 + offset);
            car->visible = true;
            if (id && projected && frame->jump_anchor_valid[id] &&
                ReprojectSideJump(car, id, flags, projected_x, projected_y,
                    frame->jump_anchor_y[id], ram, rom)) {
                ++frame->jump_reprojected;
            }
            /* Visibility can return before the native graphics upload is
             * ready. Keep fresh side artwork during that brief handover. */
            if (id && projected && ram[0x1140 + offset] == 255 && !(flags & 0x13) &&
                (car->x < 0 || car->x >= 256)) {
                FZeroVehicle pending = *car;
                if (Reconstruct(&pending, id, ram, rom)) {
                    *car = pending;
                    car->added = true;
                    if (car->x < 0) ++frame->added_left; else ++frame->added_right;
                }
            }
        } else if (projected && !(flags & 0x13) &&
                   (car->x < -32 || car->x >= 288) &&
                   car->x > -FZERO_WIDE_MARGIN - 64 &&
                   car->x < 256 + FZERO_WIDE_MARGIN + 64 &&
                   Reconstruct(car, id, ram, rom)) {
            car->visible = car->added = true;
            if (car->x < 0) ++frame->added_left; else ++frame->added_right;
        }
    }
    PrepareShadows(frame, ram, rom);
    frame->ready = true;
}

void FZeroVehiclesApply(const FZeroVehicles *frame, Ppu *copy) {
    uint8_t hints[16] = {0};
    /* The side pass owns its OAM list. Reordering or adding its entries cannot
     * alter the authentic centre or consume its hardware sprite budget. */
    memset(copy->oam, 0, sizeof(copy->oam));
    memset(copy->highOam, 0x55, sizeof(copy->highOam));
    unsigned order[6], count = 0;
    for (unsigned id = 0; id < 6; ++id) if (frame->car[id].visible) {
        unsigned at = count++;
        while (at && frame->car[order[at - 1]].ground_y <= frame->car[id].ground_y) {
            order[at] = order[at - 1]; --at;
        }
        order[at] = id;
    }
    unsigned slot = 0;
    for (unsigned c = 0; c < count; ++c) {
        const FZeroVehicle *car = &frame->car[order[c]];
        if (car->added)
            memcpy(copy->vram + car->graphics_address, car->graphics, sizeof(car->graphics));
        for (unsigned p = 0; p < car->count; ++p) {
            unsigned high = (car->high >> (p * 2)) & 3;
            unsigned position = car->oam[p * 2];
            if ((!order[c] && !(position >> 8)) || (position == 0x8080 && (high & 1))) continue;
            copy->oam[slot * 2] = position;
            copy->oam[slot * 2 + 1] = car->oam[p * 2 + 1];
            unsigned shift = (slot & 3) * 2;
            copy->highOam[slot / 4] = (copy->highOam[slot / 4] & ~(3u << shift)) | high << shift;
            hints[slot / 8] |= 1u << (slot & 7);
            ++slot;
        }
    }
    for (unsigned p = 0; p < frame->shadow_count; ++p, ++slot) {
        copy->oam[slot * 2] = frame->shadow_oam[p * 2];
        copy->oam[slot * 2 + 1] = frame->shadow_oam[p * 2 + 1];
        unsigned shift = (slot & 3) * 2;
        unsigned high = (frame->shadow_high[p / 4] >> ((p & 3) * 2)) & 3;
        copy->highOam[slot / 4] = (copy->highOam[slot / 4] & ~(3u << shift)) | high << shift;
        hints[slot / 8] |= 1u << (slot & 7);
    }
    copy->oamaddl = copy->oamaddh = 0;
    PpuWsSetOamLeftHints(copy, hints);
    PpuWsSetOamRightHints(copy, hints);
    copy->renderFlags |= kPpuRenderFlags_NoSpriteLimits;
}
