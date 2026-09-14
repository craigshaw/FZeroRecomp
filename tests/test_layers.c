/* Invented PPU scenes only. No ROM data, source excerpts, or recordings. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fzero_layers.h"
#include "snes/snes.h"

Snes *g_snes;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;
uint16_t WsShadowTile(int l, int x, uint32_t y, uint16_t tile) { return tile; }
bool WsShadowLayerActive(int l) { return false; }
uint32_t WsShadowWorldX(int l) { return 0; }
uint32_t WsShadowPresentWorldY(int l, int x) { return 0; }
uint32_t WsShadowScrollY(int l) { return 0; }
void WsShadowOnVramWrite(uint16_t a, uint16_t v) { }

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static Ppu ppu, saved;
static FZeroLayers layers;
static uint32_t original[224][256];

static void CheckPolicy(bool active, bool hud_layout, int line) {
    ppu_runLine(&ppu, 0);
    ppu_runLine(&ppu, line);
    memcpy(&saved, &ppu, sizeof(ppu));
    FZeroLayersProcessLine(&layers, &ppu, line, active, hud_layout);
    CHECK(!memcmp(&saved, &ppu, sizeof(ppu)));
    for (int x = 0; x < 256; ++x) {
        uint32_t pixel = layers.hud[line - 1][x] ? layers.hud[line - 1][x] : layers.world[line - 1][x];
        CHECK((pixel & 0xffffff) == (original[line - 1][x] & 0xffffff));
        if (!layers.move_hud) {
            CHECK(layers.wide_world[line-1][x+FZERO_WIDE_MARGIN] == layers.world[line-1][x]);
            CHECK(layers.wide_hud[line-1][x+FZERO_WIDE_MARGIN] == layers.hud[line-1][x]);
        }
    }
}

static void CheckAt(bool active, int line) { CheckPolicy(active, active, line); }
static void CheckLine(bool active) { CheckAt(active, 1); }

static void SetSprite(int slot, int x, int y, bool large, unsigned attributes) {
    unsigned shift = (slot & 3) * 2;
    ppu.oam[slot * 2] = (y << 8) | (x & 255);
    ppu.oam[slot * 2 + 1] = attributes;
    ppu.highOam[slot / 4] = (ppu.highOam[slot / 4] & ~(3u << shift)) |
        (((((unsigned)x & 511) >> 8) | (large ? 2 : 0)) << shift);
}

static void CheckWideSprites(void) {
    ppu_reset(&ppu);
    PpuBeginDrawing(&ppu, (uint8_t *)original, sizeof(original[0]), kPpuRenderFlags_NewRenderer);
    ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 0x10;
    ppu.obsel = 3 << 5; /* 16 and 32 pixel pieces. */
    ppu.cgram[0] = 0x001f;
    ppu.cgram[193] = 0x7c1f;
    for (int slot = 0; slot < 128; ++slot) SetSprite(slot, 384, 128, true, 0);
    /* Distinct rows and columns exercise tile selection and both flips. */
    for (int tile = 0; tile < 256; ++tile)
        for (int row = 0; row < 8; ++row)
            ppu.vram[tile * 16 + row] = (0x81u >> (row % 3)) ^ (tile & 0xff);
    const int slots[] = {68, 75, 115, 116, 125, 126, 127};
    for (unsigned s = 0; s < sizeof(slots) / sizeof(slots[0]); ++s) {
        int slot = slots[s];
        for (int large = 0; large < 2; ++large)
            for (int flip = 0; flip < 4; ++flip)
                for (int math = 0; math < 2; ++math) {
                    ppu.cgadsub = math ? 0x90 : 0; /* OBJ subtract fixed red. */
                    ppu.fixedColor = 15;
                    unsigned attributes = 0x3800 | (flip << 14);
                    int size = large ? 32 : 16;
                    SetSprite(slot, 96, 100, large, attributes);
                    CheckAt(true, 104);
                    uint32_t reference[32];
                    memcpy(reference, original[103] + 96, size * sizeof(uint32_t));
                    for (int x = -80; x <= 335; ++x) {
                        SetSprite(slot, x, 100, large, attributes);
                        CheckAt(true, 104);
                        for (int out = 0; out < FZERO_WIDE_WIDTH; ++out) {
                            int screen = out - FZERO_WIDE_MARGIN;
                            if (screen >= 0 && screen < 256) continue;
                            /* Encoded origins beyond the right output edge
                             * decode to far-left coordinates, also invisible. */
                            uint32_t expected = screen >= x && screen < x + size ?
                                reference[screen - x] : 0xff0000;
                            if (layers.wide_world[103][out] != expected)
                                fprintf(stderr, "slot=%d size=%d flip=%d math=%d x=%d screen=%d actual=%08x expected=%08x\n",
                                    slot, size, flip, math, x, screen, layers.wide_world[103][out], expected);
                            CHECK(layers.wide_world[103][out] == expected);
                            CHECK(!layers.wide_hud[103][out]);
                        }
                    }
                }
        SetSprite(slot, 384, 128, true, 0);
    }
    ppu.cgadsub = 0;
    /* Unused vehicle pieces can retain visible tiles and a stale X high bit. */
    for (int slot = 68; slot < 116; ++slot) {
        SetSprite(slot, 256 + (slot & 7), 0, false, 0x3800);
        CheckAt(true, 4);
        for (int x = FZERO_WIDE_MARGIN + 256; x < FZERO_WIDE_WIDTH; ++x)
            CHECK(layers.wide_world[3][x] == 0xff0000);
        SetSprite(slot, 384, 128, true, 0);
    }
    /* No HUD or other unclassified slot can appear on either side. */
    for (int slot = 0; slot < 128; ++slot) {
        if (slot >= 68) continue;
        for (int side = 0; side < 2; ++side) {
            SetSprite(slot, side ? 256 : -8, 100, true, 0x3800);
            CheckAt(true, 104);
            for (int x = 0; x < FZERO_WIDE_WIDTH; ++x)
                if (x < FZERO_WIDE_MARGIN || x >= FZERO_WIDE_MARGIN + 256)
                    CHECK(layers.wide_world[103][x] == 0xff0000);
        }
        SetSprite(slot, 384, 128, true, 0);
    }
    /* A full native sprite budget cannot suppress a later side shadow. */
    for (int row = 0; row < 8; ++row) ppu.vram[row] = 0xff;
    for (int slot = 68; slot < 100; ++slot) SetSprite(slot, 32, 100, false, 0x3800);
    SetSprite(116, 256, 100, false, 0x3800);
    CheckAt(true, 104);
    CHECK(layers.wide_world[103][FZERO_WIDE_MARGIN + 256] == 0xff00ff);
    for (int slot = 68; slot < 126; ++slot) SetSprite(slot, 384, 128, true, 0);
    /* Parked entries must stay hidden even with the largest OBJ size. */
    ppu.obsel = 2 << 5;
    SetSprite(68, 384, 128, true, 0x3800);
    SetSprite(116, 384, 128, true, 0x3800);
    CheckAt(true, 132);
    for (int x = 0; x < FZERO_WIDE_MARGIN; ++x)
        CHECK(layers.wide_world[131][x] == 0xff0000);
    /* An unclassified layout still protects the full width. */
    SetSprite(68, -8, 100, false, 0x3800);
    CheckPolicy(true, false, 104);
    for (int x = 0; x < FZERO_WIDE_WIDTH; ++x) {
        CHECK(!layers.wide_world[103][x]);
        CHECK(layers.wide_hud[103][x] >> 24);
    }
    /* A repair animation reusing a HUD slot protects only its visible pixels.
     * The surrounding scene and both margins keep their effect eligibility. */
    ppu.cgadsub = 0x90; ppu.fixedColor = 15;
    SetSprite(22, 80, 100, false, 0x3800);
    CheckAt(true, 104);
    CHECK(layers.hud[103][80] == (original[103][80] | 0xff000000u));
    CHECK(!layers.world[103][80]);
    CHECK(!layers.hud[103][20] && layers.world[103][20] == 0xff0000);
    CHECK(!layers.wide_hud[103][0] && !layers.wide_hud[103][FZERO_WIDE_WIDTH - 1]);
    SetSprite(22, 384, 128, true, 0);
    CheckAt(true, 104);
    CHECK(!layers.hud[103][80] && layers.world[103][80] == 0xff0000);
    /* Full native effects can use slots outside the racing vehicle groups.
     * Extend actual edge-crossing pieces without reinterpreting signed X. */
    ppu.cgadsub=0; ppu.obsel=3<<5;
    for (int slot=0;slot<128;++slot) SetSprite(slot,256,240,false,0);
    for (int tile=0;tile<256;++tile)
        for (int row=0;row<8;++row) ppu.vram[tile*16+row]=0xff;
    layers.native_oam=true;
    SetSprite(0,-8,100,false,0x3800);
    SetSprite(1,252,100,false,0x3800);
    SetSprite(2,300,100,false,0x3800);
    CheckPolicy(true,false,104);
    CHECK(layers.wide_hud[103][FZERO_WIDE_MARGIN-8]==0xffff00ff);
    CHECK(layers.wide_hud[103][FZERO_WIDE_MARGIN+256]==0xffff00ff);
    CHECK(layers.wide_hud[103][FZERO_WIDE_MARGIN+300]==0xffff0000);
    ppu.inidisp=0;
    CheckPolicy(true,false,104);
    for(int x=0;x<FZERO_WIDE_WIDTH;++x) CHECK(layers.wide_hud[103][x]==0xff000000);
    ppu.inidisp=15;
    CheckPolicy(false,false,104);
    CHECK(layers.wide_hud[103][FZERO_WIDE_MARGIN-8]==0xff000000);
    layers.native_oam=false;
    CheckAt(true,104);
    CHECK(layers.wide_world[103][FZERO_WIDE_MARGIN-8]==0xff0000);
    CHECK(!layers.wide_hud[103][FZERO_WIDE_MARGIN-8]);
}

/* Recover the actual background beneath moved pixels, including an overlapping
 * scene sprite. Keep messages centred and preserve the native surfaces. */
static void CheckMovedHud(void) {
    ppu_reset(&ppu);
    PpuBeginDrawing(&ppu, (uint8_t *)original, sizeof(original[0]), kPpuRenderFlags_NewRenderer);
    ppu.inidisp=15; ppu.bgmode=1; ppu.screenEnabled[0]=0x10;
    ppu.cgram[0]=0x001f; ppu.cgram[129]=0x03e0; ppu.cgram[145]=0x7c00;
    for(int slot=0;slot<128;++slot) SetSprite(slot,256,240,false,0);
    for(int row=0;row<8;++row) ppu.vram[row]=0xff;
    SetSprite(24,24,100,false,0x3000);
    SetSprite(32,220,100,false,0x3000);
    SetSprite(0,120,100,false,0x3000);
    SetSprite(68,24,100,false,0x3200);
    layers.move_hud=true;
    CheckAt(true,104);
    for(int x=0;x<8;++x) {
        CHECK(layers.wide_hud[103][24+x]==0xff00ff00);
        CHECK(layers.wide_hud[103][220+2*FZERO_WIDE_MARGIN+x]==0xff00ff00);
        CHECK(layers.wide_world[103][24+FZERO_WIDE_MARGIN+x]==0x0000ff);
        CHECK(!layers.wide_hud[103][24+FZERO_WIDE_MARGIN+x]);
        CHECK(layers.wide_world[103][220+FZERO_WIDE_MARGIN+x]==0xff0000);
        CHECK(layers.wide_hud[103][120+FZERO_WIDE_MARGIN+x]==0xff00ff00);
    }
    /* The scene spark between boost and rank slots stays attached to the
     * car on both sides, including flips and both sprite sizes. */
    for(int side=0;side<2;++side) for(int large=0;large<2;++large)
        for(int flip=0;flip<4;++flip) {
            int spark_x=side?144:88;
            SetSprite(47,spark_x,100,large,0x3000|(flip<<14));
            CheckAt(true,104);
            for(int x=0;x<8;++x) {
                CHECK(!layers.hud[103][spark_x+x]);
                CHECK(!layers.wide_hud[103][spark_x+FZERO_WIDE_MARGIN+x]);
                CHECK(layers.wide_world[103][spark_x+FZERO_WIDE_MARGIN+x]==original[103][spark_x+x]);
                CHECK(!layers.wide_hud[103][spark_x+(side?2*FZERO_WIDE_MARGIN:0)+x]);
            }
        }
    /* Equal-colour overlaps retain native OAM priority around the excluded
     * slot. An earlier instrument moves; a later one hidden by the spark does not. */
    for(int slot=46;slot<=48;slot+=2) {
        SetSprite(47,144,100,false,0x3000);
        SetSprite(slot,144,100,false,0x3000);
        CheckAt(true,104);
        CHECK(!!layers.wide_hud[103][144+2*FZERO_WIDE_MARGIN]==(slot==46));
        CHECK(layers.wide_world[103][144+FZERO_WIDE_MARGIN]==0x00ff00);
        SetSprite(slot,256,240,false,0);
    }
    SetSprite(47,256,240,false,0);
    /* Racing uploads use the tail slots for shadows. They must stay under
     * the car on either side, including while another HUD piece is moved. */
    for(int slot=126;slot<128;++slot) for(int side=0;side<2;++side) {
        int x=side?144:88;
        ppu.cgram[161]=0;
        SetSprite(slot,x,100,false,0x3400);
        CheckAt(true,104);
        for(int px=0;px<8;++px) {
            CHECK(!layers.hud[103][x+px]);
            CHECK(!layers.wide_hud[103][x+FZERO_WIDE_MARGIN+px]);
            CHECK(layers.wide_world[103][x+FZERO_WIDE_MARGIN+px]==0);
            CHECK(!layers.wide_hud[103][x+(side?2*FZERO_WIDE_MARGIN:0)+px]);
        }
        /* A full native upload can reuse the same slots for counters. */
        layers.native_oam=true;
        CheckPolicy(true,false,104);
        CHECK(layers.wide_hud[103][x+(side?2*FZERO_WIDE_MARGIN:0)]==0xff000000u);
        CHECK(layers.wide_hud[103][x+FZERO_WIDE_MARGIN]==0xffff0000u);
        layers.native_oam=false;
        CheckAt(true,104);
        CHECK(!layers.wide_hud[103][x+(side?2*FZERO_WIDE_MARGIN:0)]);
        SetSprite(slot,256,240,false,0);
    }
    /* A full native effect keeps every scene pixel protected while the
     * instruments retain their wide positions, including during a fade. */
    layers.native_oam=true;
    for(int brightness=15;brightness>=0;brightness-=5) {
        ppu.inidisp=brightness;
        CheckPolicy(true,false,104);
        uint32_t channel=brightness*17;
        CHECK(layers.wide_hud[103][24]==(0xff000000u|channel<<8));
        CHECK(layers.wide_hud[103][220+2*FZERO_WIDE_MARGIN]==(0xff000000u|channel<<8));
        CHECK(layers.wide_hud[103][24+FZERO_WIDE_MARGIN]==(0xff000000u|channel));
        CHECK(layers.wide_hud[103][120+FZERO_WIDE_MARGIN]==(0xff000000u|channel<<8));
        for(int x=0;x<FZERO_WIDE_WIDTH;++x) {
            CHECK(layers.wide_hud[103][x]>>24);
            CHECK(!layers.wide_world[103][x]);
        }
    }
    ppu.inidisp=15; layers.native_oam=false;
    CheckAt(true,104);
    CHECK(!layers.wide_hud[103][24+FZERO_WIDE_MARGIN]);
    CHECK(layers.wide_world[103][24+FZERO_WIDE_MARGIN]==0x0000ff);
    /* A centred sprite wins when its palette and priority match a HUD piece
     * behind it. The shared colour must not create a duplicate at the edge. */
    SetSprite(0,24,100,false,0x3000);
    CheckAt(true,104);
    CHECK(!layers.wide_hud[103][24]);
    CHECK(layers.wide_hud[103][24+FZERO_WIDE_MARGIN]==0xff00ff00);
    SetSprite(0,120,100,false,0x3000);
    /* A later frame can return to the centred layout without stale pixels. */
    layers.move_hud=false;
    CheckAt(true,104);
    CHECK(!layers.wide_hud[103][24]);
    CHECK(layers.wide_hud[103][24+FZERO_WIDE_MARGIN]==0xff00ff00);
    /* Invented BG3 lettering spanning the overlapping source/destination
     * ranges. All sources must be cleared before any destination is written. */
    for(int slot=0;slot<128;++slot) SetSprite(slot,256,240,false,0);
    ppu.screenEnabled[0]=4; ppu.bgXsc[2]=4; ppu.cgram[1]=0x03e0;
    for(int i=0;i<1024;++i) ppu.vram[0x400+i]=0;
    layers.move_hud=true;
    CheckAt(true,8);
    for(int x=0;x<128;++x) {
        CHECK(layers.wide_hud[7][x]==0xff00ff00);
        CHECK(layers.wide_hud[7][x+128+2*FZERO_WIDE_MARGIN]==0xff00ff00);
    }
    for(int x=128;x<128+2*FZERO_WIDE_MARGIN;++x) {
        CHECK(!layers.wide_hud[7][x]);
        CHECK(layers.wide_world[7][x]==0xff0000);
    }
    /* Power fill uses a colour window. The old site must reveal the sky
     * while both dark and bright fill colours retain opaque coverage. */
    ppu.screenEnabled[0]=0; ppu.windowsel=0x200000;
    ppu.window1left=180; ppu.window1right=230;
    ppu.cgwsel=0x90; ppu.cgadsub=0x20; ppu.cgram[0]=0x001f;
    for(int colour=0;colour<2;++colour) {
        ppu.fixedColor=colour?0x03e0:0;
        CheckAt(true,20);
        for(int x=174;x<242;++x) {
            if(x>=180 && x<=230)
                CHECK(layers.wide_hud[19][x+2*FZERO_WIDE_MARGIN]==
                      (colour?0xff00ff00:0xff000000));
            CHECK(layers.wide_world[19][x+FZERO_WIDE_MARGIN]==0xff0000);
            CHECK(!layers.wide_hud[19][x+FZERO_WIDE_MARGIN]);
        }
    }
    /* Even a black meter against a black sky stays opaque at its new site. */
    ppu.cgram[0]=ppu.fixedColor=0;
    CheckAt(true,20);
    CHECK(layers.wide_hud[19][190+2*FZERO_WIDE_MARGIN]==0xff000000);
    /* A centred repair piece over the original meter must stay there. */
    ppu.screenEnabled[0]=0x10; ppu.cgwsel=0x10;
    SetSprite(0,184,18,false,0x3000);
    CheckAt(true,20);
    CHECK(layers.wide_hud[19][184+FZERO_WIDE_MARGIN]==0xff00ff00);
    CHECK(!layers.wide_hud[19][184+2*FZERO_WIDE_MARGIN]);
    /* Forced blank retains the established fallback, with no relocated ink. */
    ppu.inidisp=0x80;
    CheckAt(true,20);
    for(int x=0;x<FZERO_WIDE_WIDTH;++x) CHECK(layers.wide_hud[19][x]==0xff000000);
    layers.move_hud=false;
}

static void CheckNativeCounters(void) {
    for(int intro=0;intro<2;++intro) {
        memset(&layers,0,sizeof(layers));
        ppu_reset(&ppu);
        PpuBeginDrawing(&ppu,(uint8_t*)original,sizeof(original[0]),kPpuRenderFlags_NewRenderer);
        ppu.inidisp=15; ppu.bgmode=1; ppu.screenEnabled[0]=0x10;
        ppu.cgram[0]=0x001f; ppu.cgram[129]=0x03e0;
        for(int slot=0;slot<128;++slot) SetSprite(slot,256,240,false,0);
        for(int row=0;row<8;++row) ppu.vram[row]=0xff;
        layers.native_oam=true; layers.move_hud=true;
        layers.intro_panorama=intro; layers.results_layout=!intro;
        SetSprite(126,208,192,false,0x3000);
        SetSprite(127,232,192,false,0x3000);
        /* Earlier slots are intro/result lettering, even at a corner. */
        SetSprite(24,24,192,false,0x3000);
        SetSprite(40,112,192,false,0x3000);
        CheckPolicy(true,false,196);
        CHECK(layers.wide_hud[195][208+2*FZERO_WIDE_MARGIN]==0xff00ff00u);
        CHECK(layers.wide_hud[195][232+2*FZERO_WIDE_MARGIN]==0xff00ff00u);
        CHECK(!layers.wide_hud[195][208+FZERO_WIDE_MARGIN]);
        CHECK(layers.wide_world[195][208+FZERO_WIDE_MARGIN]==0xff0000);
        CHECK(layers.wide_hud[195][24+FZERO_WIDE_MARGIN]==0xff00ff00u);
        CHECK(layers.wide_hud[195][112+FZERO_WIDE_MARGIN]==0xff00ff00u);
        CHECK(!layers.wide_hud[195][24]);
        /* Practice course selection reuses all eight tail slots for a map.
         * Its last two pieces are not lives and must stay with the others. */
        SetSprite(125,24,192,false,0x3000);
        SetSprite(126,24,200,false,0x3000);
        SetSprite(127,32,200,false,0x3000);
        CheckPolicy(true,false,204);
        CHECK(layers.wide_hud[203][24+FZERO_WIDE_MARGIN]==0xff00ff00u);
        CHECK(layers.wide_hud[203][32+FZERO_WIDE_MARGIN]==0xff00ff00u);
        CHECK(!layers.wide_hud[203][24] && !layers.wide_hud[203][32]);
        SetSprite(125,256,240,false,0);
        SetSprite(126,208,192,false,0x3000);
        SetSprite(127,232,192,false,0x3000);
        /* BG3 lettering uncovered by the moved lives icon keeps its colour
         * protection at the original position. */
        ppu.screenEnabled[0]=0x14; ppu.bgXsc[2]=4; ppu.cgram[1]=0x7c00;
        for(int tile=0;tile<1024;++tile) ppu.vram[0x400+tile]=0;
        CheckPolicy(true,false,196);
        CHECK(layers.wide_hud[195][208+FZERO_WIDE_MARGIN]==0xff0000ffu);
        CHECK(layers.wide_hud[195][208+2*FZERO_WIDE_MARGIN]==0xff00ff00u);
        /* Only the results score in the upper-left BG3 band moves. Intro
         * lettering, the results heading and the old power region stay put. */
        ppu.screenEnabled[0]=4; ppu.bgXsc[2]=4; ppu.cgram[1]=0x7c00;
        for(int tile=0;tile<1024;++tile) ppu.vram[0x400+tile]=0;
        CheckPolicy(true,false,20);
        CHECK(layers.wide_hud[19][24+(intro?FZERO_WIDE_MARGIN:0)]==0xff0000ffu);
        if(!intro) {
            CHECK(!layers.wide_hud[19][24+FZERO_WIDE_MARGIN]);
            CHECK(layers.wide_world[19][24+FZERO_WIDE_MARGIN]==0xff0000);
        }
        CHECK(layers.wide_hud[19][100+FZERO_WIDE_MARGIN]==0xff0000ffu);
        CHECK(layers.wide_hud[19][190+FZERO_WIDE_MARGIN]==0xff0000ffu);
        CHECK(!layers.wide_hud[19][190+2*FZERO_WIDE_MARGIN]);
        ppu.screenEnabled[0]=0;
        CheckPolicy(true,false,20);
        CHECK(!layers.hud[19][190]); /* No racing power-window mask. */
    }
    memset(&layers,0,sizeof(layers));
}

static void CheckCrashFilter(void) {
    memset(&layers,0,sizeof(layers));
    ppu_reset(&ppu);
    PpuBeginDrawing(&ppu,(uint8_t*)original,sizeof(original[0]),kPpuRenderFlags_NewRenderer);
    ppu.bgmode=7; ppu.screenEnabled[0]=0x10;
    ppu.cgram[0]=0x001f; ppu.cgram[129]=0x03e0; ppu.cgram[145]=0x7c00;
    for(int row=0;row<8;++row) ppu.vram[row]=0xff;
    layers.native_oam=true; layers.move_hud=true;
    layers.crash_layout=true;
    /* Early and full-upload explosion pieces reuse the old rank slots.
     * All pieces stay in the filtered scene at their live position. */
    const int effects[]={47,48,49,50,51,68,125,126,127};
    for(int native=0;native<2;++native)
    for(unsigned i=0;i<sizeof(effects)/sizeof(effects[0]);++i)
    for(int brightness=15;brightness>=0;brightness-=5) {
        layers.native_oam=native;
        for(int slot=0;slot<128;++slot) SetSprite(slot,256,240,false,0);
        SetSprite(0,120,96,false,0x3000); /* Centred message. */
        SetSprite(24,24,96,false,0x3000);
        SetSprite(32,220,96,false,0x3000);
        SetSprite(effects[i],24,96,false,0x3200); /* Effect below moved HUD. */
        ppu.inidisp=brightness;
        CheckPolicy(true,true,100);
        uint32_t channel=brightness*17;
        CHECK(layers.hud[99][120]==(0xff000000u|channel<<8));
        CHECK(layers.wide_hud[99][24]==(0xff000000u|channel<<8));
        CHECK(!layers.wide_hud[99][24+FZERO_WIDE_MARGIN]);
        CHECK(layers.wide_world[99][24+FZERO_WIDE_MARGIN]==channel);
        SetSprite(effects[i],80,96,false,0x3200);
        CheckPolicy(true,true,100);
        CHECK(!layers.hud[99][80] && layers.world[99][80]==channel);
        CHECK(!layers.wide_hud[99][80+FZERO_WIDE_MARGIN]);
        CHECK(layers.wide_world[99][80+FZERO_WIDE_MARGIN]==channel);
        CHECK(!layers.wide_hud[99][80]);
        CHECK(!layers.wide_hud[99][0]);
        CHECK(layers.wide_world[99][0]==channel<<16);
    }
    memset(&layers,0,sizeof(layers));
}

static void CheckGpEndingHud(void) {
    memset(&layers,0,sizeof(layers));
    ppu_reset(&ppu);
    PpuBeginDrawing(&ppu,(uint8_t*)original,sizeof(original[0]),kPpuRenderFlags_NewRenderer);
    ppu.inidisp=15; ppu.bgmode=1; ppu.screenEnabled[0]=0x10;
    ppu.cgram[0]=0x001f; ppu.cgram[129]=0x03e0; ppu.cgram[145]=0x7c00;
    for(int slot=0;slot<128;++slot) SetSprite(slot,256,240,false,0);
    for(int row=0;row<8;++row) ppu.vram[row]=0xff;
    SetSprite(24,24,8,false,0x3000);
    SetSprite(32,220,8,false,0x3000);
    SetSprite(68,24,8,false,0x3200);
    /* Result rows can reuse slots that contain instruments during a race. */
    SetSprite(40,64,80,false,0x3000);
    SetSprite(0,120,80,false,0x3000);
    layers.results_layout=true; layers.move_hud=true;
    CheckPolicy(true,false,12);
    CHECK(layers.wide_hud[11][24]==0xff00ff00u);
    CHECK(layers.wide_hud[11][220+2*FZERO_WIDE_MARGIN]==0xff00ff00u);
    CHECK(!layers.wide_hud[11][24+FZERO_WIDE_MARGIN]);
    CHECK(layers.wide_world[11][24+FZERO_WIDE_MARGIN]==0x0000ff);
    CHECK(!layers.wide_hud[11][220+FZERO_WIDE_MARGIN]);
    CHECK(layers.wide_world[11][220+FZERO_WIDE_MARGIN]==0xff0000);
    for(int mode=1;mode<=7;mode+=6)
        for(int row=48;row<=80;row+=32) {
            ppu.bgmode=mode;
            SetSprite(40,64,row,false,0x3000);
            SetSprite(0,120,row,false,0x3000);
            CheckPolicy(true,false,row+4);
            CHECK(layers.wide_hud[row+3][64+FZERO_WIDE_MARGIN]==0xff00ff00u);
            CHECK(layers.wide_hud[row+3][120+FZERO_WIDE_MARGIN]==0xff00ff00u);
            CHECK(!layers.wide_hud[row+3][64]);
        }
    /* The ending transition still has the racing map, markers, lives and
     * boost icons. Their slots later hold the centred results table. */
    const int corner_slots[]={20,21,22,23,24,25,26,27,28,29,30,31,44,45,46};
    ppu.bgmode=7;
    for(unsigned i=0;i<sizeof(corner_slots)/sizeof(corner_slots[0]);++i) {
        int slot=corner_slots[i];
        int x=(slot==22 || slot==23 || slot>=44)?216:24;
        for(int s=0;s<128;++s) SetSprite(s,256,240,false,0);
        SetSprite(slot,x,192,false,0x3000);
        SetSprite(68,x,192,false,0x3200);
        SetSprite(0,96,192,false,0x3000);
        CheckPolicy(true,false,196);
        int destination=x+(x<128?0:2*FZERO_WIDE_MARGIN);
        CHECK(layers.wide_hud[195][destination]==0xff00ff00u);
        CHECK(!layers.wide_hud[195][x+FZERO_WIDE_MARGIN]);
        CHECK(layers.wide_world[195][x+FZERO_WIDE_MARGIN]==0x0000ff);
        CHECK(layers.wide_hud[195][96+FZERO_WIDE_MARGIN]==0xff00ff00u);
        /* Reuse the same slot for results lettering. Even a low table row
         * must stay centred, rather than inheriting the old instrument move. */
        SetSprite(slot,112,192,false,0x3000);
        CheckPolicy(true,false,196);
        CHECK(layers.wide_hud[195][112+FZERO_WIDE_MARGIN]==0xff00ff00u);
        CHECK(!layers.wide_hud[195][112]);
    }
    /* The colour-window meter moves with its opaque fill. The uncovered sky
     * remains filterable, including when the fill is black. */
    ppu.bgmode=1; ppu.screenEnabled[0]=0; ppu.windowsel=0x200000;
    ppu.window1left=180; ppu.window1right=230;
    ppu.cgwsel=0x90; ppu.cgadsub=0x20;
    for(int colour=0;colour<2;++colour) {
        ppu.fixedColor=colour?0x03e0:0;
        CheckPolicy(true,false,20);
        CHECK(layers.hud[19][190]==(colour?0xff00ff00u:0xff000000u));
        CHECK(layers.wide_hud[19][190+2*FZERO_WIDE_MARGIN]==
              (colour?0xff00ff00u:0xff000000u));
        CHECK(!layers.wide_hud[19][190+FZERO_WIDE_MARGIN]);
        CHECK(layers.wide_world[19][190+FZERO_WIDE_MARGIN]==0xff0000);
    }
    memset(&layers,0,sizeof(layers));
}

void TestVehicles(void);
void TestGround(void);

static void CheckTextFilters(bool results) {
    memset(&layers, 0, sizeof(layers));
    ppu_reset(&ppu);
    PpuBeginDrawing(&ppu, (uint8_t *)original, sizeof(original[0]), kPpuRenderFlags_NewRenderer);
    ppu.inidisp=15; ppu.bgmode=1; ppu.screenEnabled[0]=0x10;
    ppu.cgram[0]=0x001f; ppu.cgram[129]=ppu.cgram[193]=0x03e0;
    for(int slot=0;slot<128;++slot) SetSprite(slot,384,128,false,0);
    for(int row=0;row<8;++row) ppu.vram[0x300+row]=0xff;
    layers.intro_panorama=!results; layers.results_layout=results; layers.native_oam=true;
    /* Intro and frozen result lettering use the native sprite list.
     * Protect it at the centre and either edge while filtering the backdrop. */
    const int positions[]={64,-4,252};
    for(int palette=0;palette<=4;palette+=4)
    for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);++i) {
        int origin=positions[i];
        SetSprite(60,origin,80,false,0x3030|(palette<<9));
        CheckPolicy(true,false,84);
        for(int x=0;x<FZERO_WIDE_WIDTH;++x) {
            int native=x-FZERO_WIDE_MARGIN;
            bool text=native>=origin && native<origin+8;
            CHECK(layers.wide_hud[83][x]==(text?0xff00ff00u:0));
            if(!text) CHECK(layers.wide_world[83][x]==0xff0000);
        }
    }
    /* The intro has no racing power meter, even at its usual coordinates. */
    CheckPolicy(true,false,20);
    CHECK(!layers.hud[19][180] && layers.world[19][180]==0xff0000);
    /* BG3 lettering is protected below the racing HUD's top band too. */
    ppu.screenEnabled[0]=4; ppu.bgXsc[2]=4; ppu.cgram[1]=0x7c00;
    for(int tile=0;tile<1024;++tile) ppu.vram[0x400+tile]=0;
    for(int row=0;row<8;++row) ppu.vram[row]=0xaa;
    CheckPolicy(true,false,84);
    CHECK(layers.hud[83][0]==0xff0000ffu && !layers.hud[83][1]);
    CHECK(layers.world[83][1]==0xff0000);
    if(results) {
        /* The GP ending retains racing cars behind centred result lettering.
         * Cars keep the filter while text outside the normal HUD slots does not. */
        ppu.screenEnabled[0]=0x10; layers.native_oam=false;
        SetSprite(60,64,80,false,0x3030);
        SetSprite(68,96,80,false,0x3030);
        SetSprite(126,144,80,false,0x3030);
        SetSprite(127,160,80,false,0x3030);
        for(int mode=1;mode<=7;mode+=6) {
            ppu.bgmode=mode;
            CheckPolicy(true,false,84);
            CHECK(layers.hud[83][64]==0xff00ff00u);
            CHECK(!layers.hud[83][96] && layers.world[83][96]==0x00ff00);
            CHECK(!layers.hud[83][144] && layers.world[83][144]==0x00ff00);
            CHECK(!layers.hud[83][160] && layers.world[83][160]==0x00ff00);
            CHECK(!layers.hud[83][180] && !layers.wide_hud[83][0]);
        }
    }
    /* Unknown menus and unsupported rendering retain the full fallback. */
    layers.intro_panorama=false; layers.results_layout=false;
    CheckPolicy(true,false,84); CHECK(layers.hud[83][1]>>24);
    layers.intro_panorama=!results; layers.results_layout=results; ppu.inidisp=0x80;
    CheckPolicy(true,false,84); CHECK(layers.hud[83][1]>>24);
    CHECK(layers.wide_hud[83][0]==0xff000000u);
    memset(&layers,0,sizeof(layers));
}

int main(void) {
    ppu_reset(&ppu);
    PpuBeginDrawing(&ppu, (uint8_t *)original, sizeof(original[0]), kPpuRenderFlags_NewRenderer);
    ppu.inidisp = 15;
    ppu.bgmode = 9;
    ppu.screenEnabled[0] = 4;
    ppu.bgXsc[2] = 4; /* Tilemap at word 0x400, separate from character data. */
    ppu.cgram[0] = 0x001f;
    ppu.cgram[1] = 0x03e0;
    for (int i = 0; i < 1024; ++i) ppu.vram[0x400 + i] = 0x2000;
    for (int i = 0; i < 8; ++i) ppu.vram[i] = 0x00aa;
    CheckLine(true);
    CHECK(layers.hud_pixels == 128 && layers.extracted_lines == 1);
    CHECK(layers.world[0][0] == 0);
    CHECK((layers.hud[0][0] & 0xffffff) == 0x00ff00);
    /* Master brightness is retained. */
    ppu.inidisp = 7;
    CheckLine(true); CHECK(layers.hud[0][0] == 0xff007700);
    /* Inactive states protect the full image. Final colour math is retained. */
    CheckLine(false); CHECK(layers.hud[0][0] == 0xff007700);
    CHECK(layers.hud[0][1] >> 24); CHECK(!layers.world[0][1]);
    ppu.cgadsub = 4; ppu.fixedColor = 31;
    CheckLine(true); CHECK(layers.hud[0][0] == (original[0][0] | 0xff000000u));
    CHECK((layers.hud[0][0] & 0xffffff) != 0x007700);
    ppu.cgadsub = 0; ppu.cgwsel = 0x40;
    CheckLine(true); CHECK(layers.hud[0][0] >> 24);
    ppu.cgwsel = 0;
    ppu.inidisp = 0x80;
    CheckLine(true); CHECK(layers.hud[0][0] >> 24);
    /* A scene sprite over low-priority BG3 stays in the scene. */
    ppu.inidisp = 15;
    for (int i = 0; i < 1024; ++i) ppu.vram[0x400 + i] = 0;
    ppu.screenEnabled[0] = 0x14;
    for (int i = 0; i < 128; ++i) ppu.oam[i * 2] = 0xf000;
    ppu.oam[68 * 2] = 0;
    ppu.oam[68 * 2 + 1] = 0x30; /* Sprite character at word 0x300. */
    for (int i = 0; i < 8; ++i) ppu.vram[0x300 + i] = 0xff;
    ppu.cgram[129] = 0x7c00;
    CheckLine(true); CHECK(!layers.hud[0][0]);
    CHECK((layers.world[0][0] & 0xffffff) == 0x0000ff);
    /* A HUD sprite wins above that same scene sprite. */
    ppu.oam[22 * 2] = 0; ppu.oam[22 * 2 + 1] = 0x30;
    CheckLine(true); CHECK(layers.hud[0][0] == 0xff0000ff);
    /* A high-priority BG tile hiding a HUD sprite cannot leak that sprite. */
    ppu.screenEnabled[0] = 0x11; ppu.bgXsc[0] = 8;
    for (int i = 0; i < 1024; ++i) ppu.vram[0x800 + i] = 0x2000;
    CheckLine(true); CHECK(!layers.hud[0][0]);
    /* Tail counter capture in a native upload, then removal on the next frame. */
    ppu.screenEnabled[0] = 0x10;
    ppu.oam[22 * 2] = ppu.oam[68 * 2] = 0xf000;
    ppu.oam[126 * 2] = 0; ppu.oam[126 * 2 + 1] = 0x30;
    layers.native_oam=true;
    CheckLine(true); CHECK(layers.hud[0][0] == 0xff0000ff);
    ppu.oam[126 * 2] = 0xf000;
    CheckLine(true); CHECK(!layers.hud[0][0]);
    layers.native_oam=false;
    /* The power window protects even black pixels, only inside its bounds. */
    ppu.cgram[0] = 0;
    CheckAt(true, 20);
    CHECK(layers.hud[19][180] == 0xff000000);
    CHECK(!layers.hud[19][173] && !layers.hud[19][242]);
    CheckAt(false, 20); CHECK(layers.hud[19][173] == 0xff000000);
    CheckAt(true, 20); CHECK(!layers.hud[19][173]);
    /* Invented packed panoramas. Each tile has a flat colour determined by
     * its unwrapped coordinate; unused page data is deliberately empty. */
    for (int layer = 0; layer < 2; ++layer) {
        ppu_reset(&ppu);
        ppu.inidisp = 15; ppu.bgmode = 1; ppu.screenEnabled[0] = 1 << layer;
        int base = layer ? 0x7000 : 0x7800;
        int first_row = layer ? 11 : 4, first_scroll = layer ? 92 : 36;
        int period = layer ? 768 : 896;
        ppu.bgXsc[layer] = (base >> 8) | 1;
        for (int colour = 1; colour < 16; ++colour) {
            ppu.cgram[colour] = colour | (colour << 5) | (colour << 10);
            for (int row = 0; row < 8; ++row) {
                ppu.vram[colour * 16 + row] =
                    ((colour & 1) ? 0xff : 0) | ((colour & 2) ? 0xff00 : 0);
                ppu.vram[colour * 16 + row + 8] =
                    ((colour & 4) ? 0xff : 0) | ((colour & 8) ? 0xff00 : 0);
            }
        }
        for (int tile = 0; tile < period / 8; ++tile)
            for (int row = 0; row < 7; ++row)
                ppu.vram[base + (first_row + (tile / 32) * 7 + row) * 32 + tile % 32] =
                    (tile * 7 + row * 3) % 15 + 1;
        for (int position = 0; position < period; ++position) {
            ppu.hScroll[layer] = position % 256;
            ppu.vScroll[layer] = first_scroll + (position / 256) * 56;
            for (int line = 1; line <= 47; line += 23) {
                CheckPolicy(true, true, line);
                for (int x = 0; x < FZERO_WIDE_WIDTH; ++x) {
                    if (x >= FZERO_WIDE_MARGIN && x < FZERO_WIDE_MARGIN + 256) continue;
                    int u = (position + x - FZERO_WIDE_MARGIN + period) % period;
                    int row = (first_scroll + line) / 8 - first_row;
                    unsigned colour = ((u / 8) * 7 + row * 3) % 15 + 1;
                    unsigned channel = (colour << 3) | (colour >> 2);
                    CHECK(layers.wide_world[line - 1][x] == channel * 0x010101);
                    CHECK(!layers.wide_hud[line - 1][x]);
                }
            }
        }
        /* During the intro, the skyline enters above a moving horizon.
         * Vertical scroll includes that displacement as well as strip phase. */
        layers.intro_panorama=true;
        const int shifts[]={3,13,26,39}; /* Invented horizon displacements. */
        const int positions[]={0,239,255,256,511,767};
        for(unsigned d=0;d<sizeof(shifts)/sizeof(shifts[0]);++d)
            for(unsigned p=0;p<sizeof(positions)/sizeof(positions[0]);++p) {
                int position=positions[p];
                ppu.hScroll[layer]=position%256;
                ppu.vScroll[layer]=first_scroll+(position/256)*56+shifts[d];
                for(int line=1;line<=47-shifts[d];++line) {
                    CheckPolicy(true,false,line);
                    for(int x=0;x<FZERO_WIDE_WIDTH;++x) {
                        if(x>=FZERO_WIDE_MARGIN && x<FZERO_WIDE_MARGIN+256) continue;
                        int u=(position+x-FZERO_WIDE_MARGIN+period)%period;
                        int row=(first_scroll+shifts[d]+line)/8-first_row;
                        unsigned colour=((u/8)*7+row*3)%15+1;
                        unsigned channel=(colour<<3)|(colour>>2);
                        CHECK(!layers.wide_hud[line-1][x]);
                        CHECK(layers.wide_world[line-1][x]==channel*0x010101);
                    }
                }
            }
        layers.intro_panorama=false;
    }
    /* The wide Mode 7 sides sample additional map coordinates. This map has
     * deliberately different content outside the native view. */
    ppu_reset(&ppu);
    PpuBeginDrawing(&ppu,(uint8_t *)original,sizeof(original[0]),kPpuRenderFlags_NewRenderer);
    ppu.inidisp=15; ppu.bgmode=7; ppu.screenEnabled[0]=1;
    ppu.m7matrix[0]=ppu.m7matrix[3]=256;
    for (unsigned i=0;i<32768;++i)
        ppu.vram[i]=((i*13+i/128)&255) | (((i*3+i/8)%127+1)<<8);
    for(unsigned i=0;i<256;++i) ppu.cgram[i]=(i&31)|((i&31)<<5)|((i&31)<<10);
    CheckAt(true,111);
    unsigned distinct=0;
    for(int x=0;x<FZERO_WIDE_WIDTH;++x) {
        if(x>=FZERO_WIDE_MARGIN && x<FZERO_WIDE_MARGIN+256) continue;
        unsigned u=(x-FZERO_WIDE_MARGIN)&1023, v=111;
        unsigned tile=ppu.vram[(v/8)*128+u/8]&255;
        unsigned colour=(ppu.vram[tile*64+(v&7)*8+(u&7)]>>8)&31;
        unsigned channel=(colour<<3)|(colour>>2);
        if (layers.wide_world[110][x] != channel*0x010101)
            fprintf(stderr, "wide x=%d actual=%08x expected=%08x\n", x, layers.wide_world[110][x], channel*0x010101);
        CHECK(layers.wide_world[110][x] == channel*0x010101);
        CHECK(!layers.wide_hud[110][x]);
        distinct += layers.wide_world[110][x] != original[110][(x-FZERO_WIDE_MARGIN)&255];
    }
    CHECK(distinct>20);
    /* An unclassified layout protects the full width with live wide ground.
     * Returning to a known layout clears the full mask without stale pixels. */
    uint32_t left_side = layers.wide_world[110][0];
    unsigned long captured = layers.wide_lines;
    CheckPolicy(true, false, 111);
    CHECK(layers.wide_lines == captured + 1);
    for (int x = 0; x < FZERO_WIDE_WIDTH; ++x) {
        CHECK(!layers.wide_world[110][x]);
        if (x < FZERO_WIDE_MARGIN || x >= FZERO_WIDE_MARGIN + 256)
            CHECK(layers.wide_hud[110][x] == (layers.wide_capture[110][x] | 0xff000000u));
    }
    for (int x = 0; x < 256; ++x)
        CHECK(layers.hud[110][x] == (original[110][x] | 0xff000000));
    CheckAt(true, 111);
    CHECK(!layers.hud[110][100] && layers.wide_world[110][0] == left_side);
    CHECK(!layers.wide_hud[110][0] && !layers.wide_hud[110][FZERO_WIDE_WIDTH - 1]);
    /* Brightness and fallback clearing apply to both margins. */
    ppu.inidisp=0; CheckAt(true,111);
    CHECK(!layers.wide_world[110][0] && !layers.wide_world[110][FZERO_WIDE_WIDTH-1]);
    ppu.inidisp=15; CheckAt(false,111);
    CHECK(!layers.wide_world[110][0] && layers.wide_hud[110][0]==0xff000000);
    CheckAt(true,111); CHECK(!layers.wide_hud[110][0]);
    ppu.inidisp=0x80; CheckAt(true,111);
    CHECK(!layers.wide_world[110][0] && layers.wide_hud[110][0]==0xff000000);
    CheckWideSprites();
    CheckMovedHud();
    CheckTextFilters(false);
    CheckTextFilters(true);
    CheckGpEndingHud();
    CheckCrashFilter();
    CheckNativeCounters();
    TestVehicles();
    TestGround();
    puts("layer extraction tests: passed");
    return 0;
}
