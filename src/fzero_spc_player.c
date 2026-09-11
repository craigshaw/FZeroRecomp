/*
 * Minimal SpcPlayer for the F-Zero recomp.
 *
 * The runner's RtlReset() calls g_spc_player->initialize(). F-Zero uploads
 * its own SPC program through the APU ports, so the
 * player here is a minimal framework-conformant instance: the runner's APU
 * model handles the actual port-driven upload. Mirrors smw_spc_player.c's
 * shape (first member is the framework SpcPlayer struct, DSP owns our RAM).
 */
#include "spc_player.h"
#include "snes/spc.h"
#include "snes/dsp_regs.h"
#include <string.h>
#include <stdlib.h>

typedef struct FZeroSpcPlayer {
  SpcPlayer base;
  uint8 ram[65536];
} FZeroSpcPlayer;

static void Dsp_Write(FZeroSpcPlayer *p, uint8_t reg, uint8_t value) {
  if (p->base.dsp)
    dsp_write(p->base.dsp, reg, value);
}

static void FZeroSpcPlayer_Initialize(SpcPlayer *p_in) {
  FZeroSpcPlayer *p = (FZeroSpcPlayer *)p_in;
  if (p->base.dsp)
    dsp_reset(p->base.dsp);
  memset(p->ram, 0, 0x500);
  memset(p->base.input_ports, 0, sizeof(p->base.input_ports));
  /* Standard SNES DSP power-on register state. */
  static const uint8 regs[12] = { MVOLL,MVOLR,EVOLL,EVOLR,FLG,EFB,PMON,NON,EON,DIR,ESA,EDL };
  static const uint8 vals[12] = { 0x7F, 0x7F,    0,    0,0x2F,0x60,   0,  0,  0,0x80,0x60,  2 };
  for (int i = 11; i >= 0; i--)
    Dsp_Write(p, regs[i], vals[i]);
}

static void FZeroSpcPlayer_Upload(SpcPlayer *p_in, const uint8_t *data) {
  /* F-Zero boots its own SPC program via APU ports; nothing to stage here. */
  (void)p_in;
  (void)data;
}

/* The framework declares this extern in spc_player.h; the host owns it. */
SpcPlayer *g_spc_player;

SpcPlayer *FZeroSpcPlayer_Create(void) {
  FZeroSpcPlayer *p = (FZeroSpcPlayer *)calloc(1, sizeof(FZeroSpcPlayer));
  p->base.dsp = dsp_init(p->ram);
  p->base.initialize = &FZeroSpcPlayer_Initialize;
  p->base.upload = &FZeroSpcPlayer_Upload;
  return &p->base;
}
