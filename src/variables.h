#pragma once

/* The snesrecomp runner (snes/cpu.c, snes/snes.c) includes "variables.h"
 * vestigially — the file is a per-game RAM map (SMW's src/variables.h) and
 * the runner references none of its macros. F-Zero needs no RAM aliases
 * here; the recompiled code touches g_ram directly. */
