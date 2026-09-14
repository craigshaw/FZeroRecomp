/* Invented memory, tables and tile patterns only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fzero_layers.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "vehicle line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static uint8_t ram[0x20000], rom[0x80000], saved_ram[0x20000], saved_rom[0x80000];
static FZeroVehicles frame;
static FZeroLayers layers;
static Ppu ppu, saved_ppu;
static uint32_t original[224][256];
static unsigned Address(unsigned a) { return (a >> 16) * 0x8000 + (a & 0x7fff); }
static uint8_t *Data(unsigned a) { return rom + Address(a); }
static void Word(uint8_t *p, int value) { p[0] = value; p[1] = (unsigned)value >> 8; }

static void Fixture(void) {
    memset(ram, 0, sizeof(ram)); memset(rom, 0, sizeof(rom));
    memset(&frame, 0, sizeof(frame));
    ram[0x50] = 1; ram[0x54] = 2; ram[0x55] = 3; ram[0x5c] = 1; ram[0x5f] = 4;
    memset(Data(0x09ed00), 100, 658);
    memset(Data(0x09ec00), 64, 256);
    const uint8_t limits[8] = {200, 170, 140, 110, 95, 80, 70, 60};
    memcpy(Data(0x00f283), limits, sizeof(limits));
    for (unsigned i = 0; i < 25; ++i) Data(0x00f26a)[i] = i / 2;
    for (unsigned i = 0; i < 117; ++i) Data(0x02fd56)[i] = (i % 4) * 2;
    for (unsigned i = 0; i < 54; ++i) Word(Data(0x00faf1) + i * 2, 0x9000);
    Word(Data(0x009000), 0);
    Word(Data(0x009002), 0); Data(0x009004)[0] = (uint8_t)-8;
    Word(Data(0x009005), 0x3000); Word(Data(0x009007), 128);
    for (unsigned i = 0; i < 57; ++i) {
        Word(Data(0x02fdcb) + i * 2, 0x9000);
        Word(Data(0x02fe3f) + i * 2, 0x2000);
        Data(0x02feb3)[i] = Data(0x02feec)[i] = 16;
    }
    for (unsigned model = 0; model < 4; ++model)
        for (unsigned row = 0; row < 8; ++row) Word(Data(((8 + model) << 16) | 0x9000) + row * 2, 0xff);
    for (unsigned row = 0; row < 8; ++row) Word(ram + 0x2000 + row * 2, 0xff);
    for (unsigned size = 0; size < 8; ++size) {
        Data(0x0becc2)[size] = 2;
        Word(Data(0x0becd0) + size * 16, -8);
        Data(0x0becd0)[size * 16 + 2] = 0x80;
        Data(0x0becd0)[size * 16 + 3] = 0x60;
    }
    for (unsigned id = 1; id < 6; ++id) {
        Word(Data(0x02fd4a) + id * 2, 0x4000 + (id - 1) * 0x200);
        Word(ram + 0xc40 + id * 2, (id - 1) * 0x20);
        Word(ram + 0x1180 + id * 2, -200);
    }
}

static void TestGpEndingVehicles(void) {
    Fixture();
    ram[0xc3] = 0x11;
    ram[0xb02] = ram[0xb04] = 0x80;
    Word(ram + 0x1172, -178); Word(ram + 0x1174, 172);
    memcpy(saved_ram, ram, sizeof(ram)); memcpy(saved_rom, rom, sizeof(rom));
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
    CHECK(frame.ready && frame.car[1].added && frame.car[2].added);
    CHECK(frame.car[1].x == -50 && frame.car[2].x == 300);
    CHECK(frame.shadow_count == 1);
    CHECK(!memcmp(ram, saved_ram, sizeof(ram)) && !memcmp(rom, saved_rom, sizeof(rom)));

    memset(&layers, 0, sizeof(layers));
    layers.results_layout = true;
    layers.vehicles = frame;
    ppu_reset(&ppu);
    PpuBeginDrawing(&ppu, (uint8_t *)original, sizeof(original[0]), kPpuRenderFlags_NewRenderer);
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.obsel = 2;
    ppu.screenEnabled[0] = 0x10;
    ppu.cgram[0] = 0x03e0; ppu.cgram[129] = 0x7c1f; ppu.cgram[145] = 0x7c00;
    /* Invented results lettering stays in the native text slots. */
    ppu.oam[0] = (92 << 8) | 64; ppu.oam[1] = 0x3200;
    for (unsigned row = 0; row < 8; ++row) ppu.vram[0x4000 + row] = 0xff;
    ppu_runLine(&ppu, 0); ppu_runLine(&ppu, 93); saved_ppu = ppu;
    FZeroLayersProcessLine(&layers, &ppu, 93, true, false);
    CHECK(!memcmp(&saved_ppu, &ppu, sizeof(ppu)));
    CHECK(layers.wide_world[92][FZERO_WIDE_MARGIN - 50] == 0xff00ff);
    CHECK(layers.wide_world[92][FZERO_WIDE_MARGIN + 300] == 0xff00ff);
    CHECK(!layers.wide_hud[92][FZERO_WIDE_MARGIN - 50]);
    CHECK(!layers.wide_hud[92][FZERO_WIDE_MARGIN + 300]);
    CHECK(layers.wide_hud[92][FZERO_WIDE_MARGIN + 64] == 0xff0000ffu);
    for (unsigned x = 0; x < 256; ++x) {
        uint32_t c = layers.wide_hud[92][FZERO_WIDE_MARGIN + x];
        if (!c) c = layers.wide_world[92][FZERO_WIDE_MARGIN + x];
        CHECK((c & 0xffffff) == (original[92][x] & 0xffffff));
    }
    /* The ending camera continues to project moving cars every frame. */
    Word(ram + 0x1172, -172); Word(ram + 0x1174, 178);
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
    CHECK(frame.car[1].x == -44 && frame.car[2].x == 306);
    /* The exception must not enable reconstruction on unrelated layouts. */
    ram[0xc3] = 9;
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(!frame.ready);
    ram[0xc3] = 0x11; ram[0x50] = 0;
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(!frame.ready);
    ram[0x50] = 1; ram[0x54] = 3;
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(!frame.ready);
    memset(&layers, 0, sizeof(layers));
}

void TestVehicles(void) {
    uint8_t rows[658], scales[256];
    for (unsigned i = 0; i < 658; ++i) rows[i] = i % 256;
    memset(scales, 37, sizeof(scales));
    int x = 0, y = 0;
    CHECK(!FZeroVehicleProject(1, -640, rows, scales, &x, &y));
    CHECK(!FZeroVehicleProject(1, 19, rows, scales, &x, &y));
    CHECK(FZeroVehicleProject(-193, -639, rows, scales, &x, &y));
    CHECK(x == 17 && y == 0);
    CHECK(FZeroVehicleProject(193, 18, rows, scales, &x, &y));
    CHECK(x == 239 && y == 145);
    Fixture();
    ram[0xb02] = ram[0xb04] = 0x80;
    Word(ram + 0x1172, -178); Word(ram + 0x1174, 172);
    memcpy(saved_ram, ram, sizeof(ram)); memcpy(saved_rom, rom, sizeof(rom));
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
    CHECK(!memcmp(ram, saved_ram, sizeof(ram)) && !memcmp(rom, saved_rom, sizeof(rom)));
    CHECK(frame.ready && frame.car[1].added && frame.car[2].added);
    CHECK(frame.car[1].x == -50 && frame.car[2].x == 300);
    CHECK(frame.car[1].ground_y == 100 && frame.car[1].size == 4);
    CHECK(frame.added_left == 1 && frame.added_right == 1 && frame.shadow_count == 1);
    /* A copied frame keeps its artwork after guest buffers are reused. */
    memset(ram + 0x2000, 0, 64); memset(Data(0x089000), 0, 64);
    CHECK(frame.car[1].graphics[0] == 0xff);
    ppu_reset(&ppu);
    PpuBeginDrawing(&ppu, (uint8_t *)original, sizeof(original[0]), kPpuRenderFlags_NewRenderer);
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.obsel = 2;
    ppu.screenEnabled[0] = 0x10; ppu.cgram[0] = 0x03e0; ppu.cgram[129] = 0x7c1f;
    layers.vehicles = frame;
    ppu_runLine(&ppu, 0); ppu_runLine(&ppu, 93);
    saved_ppu = ppu;
    FZeroLayersProcessLine(&layers, &ppu, 93, true, true);
    CHECK(!memcmp(&saved_ppu, &ppu, sizeof(ppu)));
    CHECK(layers.wide_world[92][FZERO_WIDE_MARGIN - 50] == 0xff00ff);
    CHECK(layers.wide_world[92][FZERO_WIDE_MARGIN + 300] == 0xff00ff);
    for (unsigned i = 0; i < 256; ++i) {
        uint32_t c = layers.wide_hud[92][i + FZERO_WIDE_MARGIN];
        if (!c) c = layers.wide_world[92][i + FZERO_WIDE_MARGIN];
        CHECK((c & 0xffffff) == (original[92][i] & 0xffffff));
    }
    /* A real piece reaching the top row is distinct from an unused entry. */
    layers.vehicles.car[1].oam[0] &= 255;
    ppu_runLine(&ppu, 1);
    FZeroLayersProcessLine(&layers, &ppu, 1, true, true);
    CHECK(layers.wide_world[0][FZERO_WIDE_MARGIN - 50] == 0xff00ff);
    /* Depth sorting is independent of vehicle identity and native slot order. */
    frame.car[2] = frame.car[1]; frame.car[2].ground_y = 120;
    frame.car[2].oam[1] = 0x3200;
    ppu.cgram[145] = 0x7c00;
    layers.vehicles = frame;
    FZeroLayersProcessLine(&layers, &ppu, 93, true, true);
    CHECK(layers.wide_world[92][FZERO_WIDE_MARGIN - 50] == 0x0000ff);
    frame.car[2].ground_y = 90; layers.vehicles = frame;
    FZeroLayersProcessLine(&layers, &ppu, 93, true, true);
    CHECK(layers.wide_world[92][FZERO_WIDE_MARGIN - 50] == 0xff00ff);
    /* Pose and height changes select fresh artwork and placement while culled. */
    Fixture(); ram[0xb02] = 0x80; Word(ram + 0x1172, -178);
    ram[0xbd1] = 8; ram[0xbc3] = 10;
    Data(0x08ede0)[155] = 128;
    Word(Data(0x02fdcb) + 2, 0x9100); Word(Data(0x089100), 0xee);
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
    CHECK(frame.car[1].added && frame.car[1].graphics[0] == 0xee);
    CHECK((frame.car[1].oam[0] >> 8) == 82);
    /* Native visibility takes precedence, including native animation pieces. */
    Fixture(); ram[0xb02] = 0x88;
    Word(ram + 0x1172, 120); Word(ram + 0xc52, 248); ram[0xc62] = 100;
    ram[0xc33] = 4; Word(ram + 0x320, 0x5cf8); Word(ram + 0x322, 0x3000);
    ram[0x11d2] = 1;
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
    CHECK(frame.car[1].visible && !frame.car[1].added);
    CHECK(frame.projected == 1 && frame.matched == 1 && frame.car[1].oam[0] == 0x5cf8);
    /* Re-entering native visibility must not replace fresh side artwork with
     * an old placeholder while its native graphics upload is still pending. */
    Word(ram + 0x1172, 132); Word(ram + 0xc52, 260); ram[0x1142] = 255;
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
    CHECK(frame.car[1].added && frame.car[1].graphics[0] == 0xff);
    ram[0x1142] = 0;
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(!frame.car[1].added);
    /* Offscreen traffic reads its current RAM graphics; removed cars and
     * depth-rejected cars do not return as stale sprites. */
    ram[0xb02] = 0xc0; ram[0x1133] = 7; Word(ram + 0x1172, -178);
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
    CHECK(frame.car[1].added && frame.car[1].graphics[0] == 0xff);
    /* A held pause or exit fade retains the same current side artwork. */
    FZeroVehicle held = frame.car[1];
    const unsigned phases[] = {5,4,6,3};
    for (unsigned phase=0;phase<sizeof(phases)/sizeof(phases[0]);++phase) {
        ram[0x55]=phases[phase];
        FZeroVehiclesPrepare(&frame,ram,rom,sizeof(rom));
        CHECK(frame.ready && !memcmp(&held,&frame.car[1],sizeof(held)));
    }
    ram[0xb02] |= 4;
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(!frame.car[1].visible);
    ram[0xb02] = 0x80; Word(ram + 0x1182, 19);
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(!frame.car[1].visible);
    ram[0xc3] = 1;
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(!frame.ready);
    ram[0xc3] = 0;
    FZeroVehiclesPrepare(&frame, ram, rom, 10); CHECK(!frame.ready);
    /* All four pieces of the final shadow survive, including the two pieces
     * whose native OAM slots can be overwritten by counters. */
    Fixture();
    for (unsigned id = 1; id < 6; id += 2) {
        ram[0xb00 + id * 2] = 0x88;
        Word(ram + 0xc50 + id * 2, 300); ram[0xc60 + id * 2] = 100;
        ram[0xc31 + id * 2] = 4;
    }
    for (unsigned p = 0; p < 4; ++p) {
        uint8_t *part = Data(0x0becd0) + 4 * 16 + p * 4;
        Word(part, -12 + p * 8); part[2] = 0x80; part[3] = 0x60;
    }
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
    CHECK(frame.shadow_count == 12);
    for (unsigned row = 0; row < 8; ++row) ppu.vram[0x4800 + row] = 0xff;
    layers.vehicles = frame;
    ppu_runLine(&ppu, 99);
    FZeroLayersProcessLine(&layers, &ppu, 99, true, true);
    CHECK(layers.wide_world[98][FZERO_WIDE_MARGIN + 312] == 0xff00ff);
    ram[0x51] = 1;
    FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(!frame.shadow_count);
    layers.vehicles = frame;
    FZeroLayersProcessLine(&layers, &ppu, 99, true, true);
    CHECK(layers.wide_world[98][FZERO_WIDE_MARGIN + 312] == 0x00ff00);
    /* Horizontal departure during a close jump must not pin the car to the
     * old edge or put its shadow at the native departure sentinel row. */
    for (unsigned side = 0; side < 2; ++side) {
        Fixture();
        int old_x = side ? 270 : -20, projected_x = side ? 310 : -50;
        ram[0xb02] = 0x88; ram[0xd53] = 0x80; ram[0xbc3] = 40;
        memset(Data(0x09ed00), 210, 658);
        Data(0x08ede0)[45] = 128;
        Word(ram + 0x1172, projected_x - 128);
        Word(ram + 0xc52, old_x); ram[0xc62] = 210; ram[0xc33] = 0;
        ram[0x11d2] = 1;
        Word(ram + 0x320, (162 << 8) | (old_x & 255));
        Word(ram + 0x322, 0x3000); Word(ram + 0xd82, 1);
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
        ram[0xb02] = 0x98; ram[0xc62] = 255; ram[0xc33] = 8;
        memcpy(saved_ram, ram, sizeof(ram)); memcpy(saved_rom, rom, sizeof(rom));
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
        CHECK(!frame.car[1].added && frame.jump_reprojected == 1);
        CHECK(frame.car[1].x == projected_x && frame.car[1].ground_y == 210);
        CHECK((frame.car[1].oam[0] >> 8) == 162 && frame.car[1].size == 0);
        CHECK(frame.shadow_count == 1 && (frame.shadow_oam[0] >> 8) == 208);
        CHECK(!memcmp(ram, saved_ram, sizeof(ram)) && !memcmp(rom, saved_rom, sizeof(rom)));
        ppu_reset(&ppu);
        PpuBeginDrawing(&ppu, (uint8_t *)original, sizeof(original[0]), kPpuRenderFlags_NewRenderer);
        ppu.inidisp = 15; ppu.bgmode = 1; ppu.obsel = 2; ppu.screenEnabled[0] = 0x10;
        ppu.cgram[0] = 0x03e0; ppu.cgram[129] = 0x7c1f;
        for (unsigned row = 0; row < 8; ++row)
            ppu.vram[0x4000 + row] = ppu.vram[0x4800 + row] = 0xff;
        layers.vehicles = frame;
        const int lines[] = {163, 209};
        for (unsigned part = 0; part < 2; ++part) {
            int line = lines[part], sample_x = projected_x - (part ? 8 : 0);
            ppu_runLine(&ppu, 0); ppu_runLine(&ppu, line); saved_ppu = ppu;
            FZeroLayersProcessLine(&layers, &ppu, line, true, true);
            CHECK(!memcmp(&saved_ppu, &ppu, sizeof(ppu)));
            CHECK(layers.wide_world[line - 1][FZERO_WIDE_MARGIN + sample_x] == 0xff00ff);
            for (unsigned x = 0; x < 256; ++x) {
                uint32_t c = layers.wide_hud[line - 1][x + FZERO_WIDE_MARGIN];
                if (!c) c = layers.wide_world[line - 1][x + FZERO_WIDE_MARGIN];
                CHECK((c & 0xffffff) == (original[line - 1][x] & 0xffffff));
            }
        }
        /* Retain the native departure motion, even when world height freezes.
         * The shadow follows the ground, not the animated sprite's descent. */
        Word(ram + 0x320, (178 << 8) | (old_x & 255));
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
        CHECK((frame.car[1].oam[0] >> 8) == 178 && (frame.shadow_oam[0] >> 8) == 208);
        /* Camera movement changes the ground anchor as well as the body. */
        memset(Data(0x09ed00), 214, 658);
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
        CHECK((frame.car[1].oam[0] >> 8) == 182 && (frame.shadow_oam[0] >> 8) == 212);
        memset(Data(0x09ed00), 210, 658);
        /* Native-visible artwork, ended jumps and near-plane departures keep
         * their native animation, even while its anchor is outside the view. */
        Word(ram + 0x320, (160 << 8) | 252); Word(ram + 0xd82, side ? 0 : 1);
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(frame.car[1].x == old_x);
        Word(ram + 0x320, (160 << 8) | (old_x & 255)); Word(ram + 0xd82, 1);
        ram[0xd53] = 0;
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(frame.car[1].x == old_x);
        ram[0xd53] = 0x80; Word(ram + 0x1182, 19);
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(frame.car[1].x == old_x);
        Word(ram + 0x1182, -200); Word(ram + 0x1172, 0);
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(frame.car[1].x == old_x);
        Word(ram + 0x1172, projected_x - 128); ram[0xb02] = 0x9a;
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(frame.car[1].x == old_x);
        ram[0xb02] = 0x9c;
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(!frame.car[1].visible);
        /* Returning to native visibility uses the uploaded landing pieces. */
        ram[0xb02] = 0x88; ram[0xd53] = 0; ram[0xc62] = 210;
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom));
        CHECK(frame.car[1].visible && !frame.car[1].added);
        CHECK(frame.car[1].oam[0] == ((160 << 8) | (old_x & 255)));
        CHECK(!frame.jump_anchor_valid[1]);
        /* A departure first seen without a prior ground anchor is not
         * guessed, including after an unsupported-state transition. */
        ram[0xb02] = 0x98; ram[0xd53] = 0x80;
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(frame.car[1].x == old_x);
        ram[0xb02] = 0x88;
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(frame.jump_anchor_valid[1]);
        ram[0xc3] = 1;
        FZeroVehiclesPrepare(&frame, ram, rom, sizeof(rom)); CHECK(!frame.jump_anchor_valid[1]);
    }
    TestGpEndingVehicles();
    puts("wide vehicle tests: passed");
}
