#pragma once
#include <stdbool.h>
#include <stdint.h>

static inline bool FZeroSceneTitle(const uint8_t *ram) {
    if (!ram || ram[0x5c] != 1 || ram[0x81] != 1) return false;
    /* Records and race selection change the menu state before the title
     * backdrop fades out. Keep it wide until its projection is cleared. */
    return (ram[0x54] == 0 && ram[0x55] <= 2) ||
        (ram[0x54] == 1 && ram[0x55] == 0);
}

static inline bool FZeroSceneIntro(const uint8_t *ram) {
    if (!ram || ram[0x54] != 2) return false;
    if (ram[0x55] <= 1) return ram[0x5c] <= 2;
    /* The final intro upload reaches race position before the countdown
     * installs the racing HUD. Keep that one intervening frame wide too. */
    return ram[0x55] == 2 && ram[0x56] == 0 && ram[0x5c] == 1 &&
        !ram[0x50] && !(ram[0x5f] & 4);
}

static inline bool FZeroSceneResults(const uint8_t *ram) {
    /* Results retain the course and raster state, including their exit fade.
     * The normal racing meter can be disabled independently of the backdrop. */
    return ram && ram[0x54] == 3 && (ram[0x55] <= 3 || ram[0x55] == 5) &&
        ram[0x5c] == 1;
}

/* Geometry remains usable during race-local crashes, finish cameras and fades.
 * An exception flag alone does not mean that the course has been unloaded. */
static inline bool FZeroSceneWide(const uint8_t *ram) {
    if (FZeroSceneTitle(ram) || FZeroSceneIntro(ram) || FZeroSceneResults(ram)) return true;
    if (!ram || ram[0x54] != 2 || ram[0x55] < 2 || ram[0x55] > 6 ||
        ram[0x5c] != 1 || !(ram[0x5f] & 4)) return false;
    switch (ram[0xc3]) {
    case 0: case 8: case 9: case 0x11:
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x80:
        return ram[0x50] != 0;
    case 0x40:
        /* The explosion changes to a full native OAM upload. */
        return true;
    default:
        return false;
    }
}
