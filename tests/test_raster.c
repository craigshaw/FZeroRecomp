/* Exercise the real host scanline walk with invented raster bands and I/O. */
#include "fzero_rtl.h"
#include "fzero_layers.h"
#include "fzero_scene.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/snes.h"
#include "snes/dma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"raster line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static Snes snes;
static Ppu ppu;
static Dma dma;
static FZeroLayers layers;
Snes *g_snes = &snes;
Ppu *g_ppu = &ppu;
Dma *g_dma = &dma;
CpuState g_cpu;
uint8 g_ram[0x20000], g_snesrecomp_last_hdmaen;
const uint8 *g_rom;
void snes_set_hdma_beam_enabled(Snes *s, bool enabled) {
    CHECK(s == &snes);
    s->hdmaBeamOff = !enabled;
}
static int targets[4], current_line, stage, dma_channel, hdma_row;
static unsigned draws, captures, irqs, pushes;
static unsigned modes[225], colours[225];

void dma_startDma(Dma *d, uint8 mask, bool hdma) {
    CHECK(snes.hdmaBeamOff && d == &dma && mask == 0xfe && hdma);
}
void SimpleHdma_Init(SimpleHdma *c, DmaChannel *d) {
    CHECK(snes.hdmaBeamOff);
    c->mode = d - dma.channel;
}
void ppu_runLine(Ppu *p, int line) {
    CHECK(snes.hdmaBeamOff && p == &ppu && line == (int)draws);
    CHECK(stage == 0 && hdma_row == line);
    current_line = line;
    modes[line] = p->bgmode;
    colours[line] = p->fixedColor;
    ++draws;
    stage = 1;
}
void FZeroLayersProcessLine(FZeroLayers *l, const Ppu *p, int line, bool r, bool h) {
    CHECK(l == &layers && p == &ppu && line == current_line && r && h);
    CHECK(stage == 1 && hdma_row == line);
    CHECK(p->bgmode == modes[line] && p->fixedColor == colours[line]);
    ++captures;
    stage = 2;
}
void SimpleHdma_DoLine(SimpleHdma *c) {
    CHECK(snes.hdmaBeamOff && stage == 2 && c->mode == dma_channel);
    if (++dma_channel == 8) {
        dma_channel = 0;
        ++hdma_row;
        stage = 0;
    }
}
void cpu_write8(CpuState *c, uint8 bank, uint16 address, uint8 value) {
    CHECK(c == &g_cpu && bank == 0 && address >= 0x1fc && address <= 0x1ff);
    g_ram[address] = value;
    ++pushes;
}
void bank_00_8601(CpuState *c) {
    CHECK(snes.hdmaBeamOff && snes.inIrq && stage == 0);
    CHECK(irqs < 4 && current_line == targets[irqs]);
    CHECK(hdma_row == current_line + 1 && pushes == (irqs + 1) * 4);
    CHECK(c->S == 0x1ff - 4);
    c->S += 4; /* Synthetic RTI. */
    snes.inIrq = false;
    ppu.fixedColor = ++irqs;
    if (irqs == 3) ppu.bgmode = 7;
    if (irqs < 4) snes.vTimer = targets[irqs];
    else { snes.vIrqEnabled = false; snes.vTimer = 100; }
}

/* Invented frame dispatch: the pending upload is followed by a menu update.
 * No cartridge code executes. */
static unsigned prepared;
static bool expected_wide, expected_hud;
void cpu_state_init(CpuState *c, uint8 *ram) { c->S = 0x1ff; }
int interp_bridge_run_scheduler(CpuState *c, uint32 entry, uint32 yield, uint16 flag) {
    memset(g_ram, 0, sizeof(g_ram));
    return 0;
}
void bank_00_80D9(CpuState *c) {
    CHECK(prepared == 2);
    CHECK(layers.wide_scene == expected_wide && layers.hud_layout == expected_hud);
    c->S += 4;
}
void FZeroVehiclesPrepare(FZeroVehicles *f, const uint8_t *ram, const uint8_t *rom, size_t size) {
    CHECK(prepared++ == 0 && ram == g_ram);
}
void FZeroGroundPrepare(FZeroGround *f, const uint8_t *ram) {
    CHECK(prepared++ == 1 && ram == g_ram);
}

static void CheckSceneTransitions(void) {
    FZeroSetLayers(&layers);
    FZeroRunOneFrameOfGame(); /* Synthetic reset, before the first upload. */
    const struct { unsigned mode, process, exception, obj; bool wide, hud; } cases[] = {
        {2,3,0,1,true,true}, {2,5,0,1,true,true}, {2,4,0,1,true,true},
        {2,3,0x40,1,true,true}, {2,3,0x40,0,true,true},
        {2,6,0x40,0,true,true}, {2,3,0x80,1,true,true},
        {2,3,0x22,1,true,true}, {2,3,0x21,1,true,true},
        {2,3,0x20,1,true,true}, {2,3,0x23,1,true,true},
        {2,3,8,1,true,true}, {2,4,0x80,1,true,true},
        {2,6,0x40,1,true,true}, {2,4,0x40,0,true,true},
        {2,3,9,1,true,true}, {2,3,0x11,1,true,false},
        {2,7,0x80,1,false,false}, {3,3,0,1,true,false},
        {3,1,9,0,true,false}, {3,5,9,0,true,false}, {3,6,0,0,false,false},
        {2,0,0,0,true,false}, {2,1,0,0,true,false},
        {1,3,0,1,false,false}, {2,3,4,1,false,false}, {2,3,0,0,false,false},
        {2,3,0,1,true,true}
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        g_ram[0x54]=cases[i].mode; g_ram[0x55]=cases[i].process;
        g_ram[0xc3]=cases[i].exception; g_ram[0x50]=cases[i].obj;
        g_ram[0x5c]=1; g_ram[0x5f]=4;
        prepared=0; expected_wide=cases[i].wide; expected_hud=cases[i].hud;
        FZeroRunOneFrameOfGame();
        CHECK(prepared==2 && !g_ram[0x54]);
        CHECK(layers.wide_scene==expected_wide && layers.hud_layout==expected_hud);
        CHECK(layers.native_oam==!cases[i].obj);
        CHECK(layers.crash_layout==(cases[i].wide && cases[i].mode==2 &&
              cases[i].exception==0x40 && !cases[i].obj));
        CHECK(layers.move_hud==(cases[i].hud || (cases[i].wide &&
              cases[i].mode==2 && (cases[i].exception==0x40 ||
              (cases[i].exception==0x11 && cases[i].obj)))));
        CHECK(layers.intro_panorama==(cases[i].mode==2 && cases[i].process<=1));
        CHECK(layers.results_layout==(cases[i].wide &&
              (cases[i].mode==3 || (cases[i].mode==2 && cases[i].exception==0x11))));
    }
    /* Intro geometry does not require the racing HUD or final horizon split.
     * Free-practice results can also disable the racing meter. */
    g_ram[0x54]=2; g_ram[0x55]=0;
    for(unsigned part=0;part<=2;++part) {
        g_ram[0x5c]=part;
        CHECK(FZeroSceneWide(g_ram));
    }
    g_ram[0x55]=2; g_ram[0x56]=0; g_ram[0x5c]=1;
    prepared=0; expected_wide=true; expected_hud=false;
    FZeroRunOneFrameOfGame();
    CHECK(layers.wide_scene && layers.intro_panorama && !layers.hud_layout);
    g_ram[0x54]=3; g_ram[0x55]=3; g_ram[0x5c]=1; g_ram[0x5f]=0x80;
    CHECK(FZeroSceneWide(g_ram));
    g_ram[0x5c]=0; CHECK(!FZeroSceneWide(g_ram));
    CHECK(!FZeroSceneWide(NULL));
    /* Title expansion begins only once its course projection is installed.
     * The Records fade retains it; the loaded Records page does not. */
    memset(g_ram,0,sizeof(g_ram));
    CHECK(!FZeroSceneTitle(g_ram));
    g_ram[0x5c]=1; g_ram[0x81]=1;
    for(int process=0;process<=2;++process) {
        g_ram[0x55]=process;
        CHECK(FZeroSceneTitle(g_ram) && FZeroSceneWide(g_ram));
        prepared=0; expected_wide=true; expected_hud=false;
        FZeroRunOneFrameOfGame();
        CHECK(!layers.move_hud && layers.native_oam);
        g_ram[0x5c]=1; g_ram[0x81]=1;
    }
    g_ram[0x55]=3; CHECK(!FZeroSceneWide(g_ram));
    g_ram[0x55]=2; g_ram[0x81]=0; CHECK(!FZeroSceneWide(g_ram));
    g_ram[0x81]=1; g_ram[0x5c]=0; CHECK(!FZeroSceneWide(g_ram));
    g_ram[0x5c]=1;
    g_ram[0x55]=1; g_ram[0x81]=0; CHECK(!FZeroSceneWide(g_ram));
    /* The title still owns the backdrop during selection's initial fade.
     * Keep the pending upload wide, then reject the unloaded/menu layout. */
    g_ram[0x54]=1; g_ram[0x55]=0; g_ram[0x81]=1;
    CHECK(FZeroSceneTitle(g_ram) && FZeroSceneWide(g_ram));
    prepared=0; expected_wide=true; expected_hud=false;
    FZeroRunOneFrameOfGame();
    CHECK(layers.native_oam && !layers.move_hud && !layers.intro_panorama);
    g_ram[0x54]=1; g_ram[0x55]=0; g_ram[0x5c]=1; g_ram[0x81]=0;
    CHECK(!FZeroSceneWide(g_ram));
    g_ram[0x5c]=0; g_ram[0x81]=1; CHECK(!FZeroSceneWide(g_ram));
    g_ram[0x5c]=1; g_ram[0x55]=1; CHECK(!FZeroSceneWide(g_ram));
    CHECK(!FZeroSceneTitle(NULL));
}

static void Run(bool enabled, int first, bool beam_enabled) {
    memset(&snes,0,sizeof(snes)); memset(&ppu,0,sizeof(ppu));
    memset(&g_cpu,0,sizeof(g_cpu)); memset(g_ram,0,sizeof(g_ram));
    snes.hdmaBeamOff = !beam_enabled;
    draws=captures=irqs=pushes=0;
    stage=dma_channel=hdma_row=0;
    targets[0]=first; targets[1]=19; targets[2]=43; targets[3]=91;
    ppu.bgmode=1;
    snes.vIrqEnabled=enabled; snes.vTimer=first;
    g_cpu.S=0x1ff;
    g_ram[0x50]=1; g_ram[0x5c]=1; g_ram[0x5f]=4;
    g_snesrecomp_last_hdmaen=0xfe;
    layers.wide_scene = layers.hud_layout = true;
    g_ram[0xc3]=1; /* Next guest state must not override the uploaded policy. */
    FZeroSetLayers(&layers);
    FZeroDrawPpuFrame();
    CHECK(snes.hdmaBeamOff == !beam_enabled && !snes.inIrq);
    CHECK(draws==225 && captures==225 && hdma_row==225);
    CHECK(irqs==(enabled?4:0) && g_cpu.S==0x1ff);
    for(int line=0;line<=224;++line) {
        unsigned colour=0;
        if(enabled) for(int i=0;i<4;++i) colour+=line>targets[i];
        CHECK(colours[line]==colour);
        CHECK(modes[line]==(enabled && line>43?7:1));
    }
}
int main(void) {
    Run(true,7,true);
    Run(true,7,false); /* Preserve a caller that already disabled beam HDMA. */
    Run(true,0,true); /* Pre-render IRQ affects the first visible row. */
    Run(false,7,true);
    CheckSceneTransitions();
    FZeroSetLayers(NULL);
    puts("raster timing tests: passed");
    return 0;
}
