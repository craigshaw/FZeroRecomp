#pragma once

#include <stdint.h>

/* Desktop host settings for the shared recomp-ui pre-boot launcher.
 *
 * Exactly the launcher-exposed fields this host can honor. The pre-boot
 * window seeds from this struct and the user's choices map back into it on
 * Play; the host then applies them (window size, fullscreen, filtering,
 * audio, input source) and persists them to config.ini [Settings] (see
 * config.c). Defaults use a 3x window, integer scaling, 48 kHz stereo audio,
 * and keyboard input. The game picture remains 256x224.
 */
typedef struct FZeroSettings {
    int output_method;   /* 0 SDL (the only backend this host implements) */
    int window_scale;    /* 1..N initial window size multiplier */
    int fullscreen;      /* 0 off, 1 borderless */
    int ignore_aspect;   /* bool: stretch to fill instead of integer scale */
    int linear_filter;   /* bool: bilinear upscale */
    int widescreen;      /* bool: experimental wider racing view */
    int visual_style;    /* 0 Original, 1 Enhanced, 2 Vivid, 3 Black & White */
    int show_fps;        /* bool: presentation rate readout */
    int enable_audio;    /* bool */
    int audio_freq;      /* Hz */
    int volume;          /* 0..100 */
    int player_src[2];   /* 0 none, 1 keyboard, 2 gamepad */
    int deadzone[2];     /* 0..100, gamepad stick deadzone */
    int skip_launcher;   /* bool: boot straight to the game next run */
} FZeroSettings;

static inline void FZeroSettingsInitDefault(
    FZeroSettings *s) {
    s->output_method = 0;
    s->window_scale = 3;
    s->fullscreen = 0;
    s->ignore_aspect = 0;
    s->linear_filter = 0;
    s->show_fps = 0;
    s->visual_style = 0;
    s->widescreen = 0;
    s->enable_audio = 1;
    s->audio_freq = 48000;
    s->volume = 100;
    s->player_src[0] = 1;   /* P1 keyboard, matching the pre-launcher host */
    s->player_src[1] = 0;
    s->deadzone[0] = 50;
    s->deadzone[1] = 50;
    s->skip_launcher = 0;
}

/* Load [Settings] from `path` over the given defaults. Missing file/keys
 * leave the struct untouched. */
void FZeroSettingsLoad(const char *path, FZeroSettings *s);

/* Persist the struct to `path` as a surgical in-place [Settings] update that
 * preserves all other sections and comments verbatim. */
void FZeroSettingsSave(const char *path, const FZeroSettings *s);

/* Fill `map` (SDL_NUM_SCANCODES entries, scancode -> runner input bit) with
 * the host's default keyboard layout. */
void FZeroKeyBindsDefaults(uint32_t *map, int n);

/* Seed `path` (keybinds.ini) with the host's default layout when missing. */
void FZeroKeyBindsWriteDefaults(const char *path);

/* Apply [player1] of `path` over `map` at startup and after launcher Play. */
void FZeroKeyBindsLoad(const char *path, uint32_t *map, int n);

void FZeroSettingsSanitize(FZeroSettings *s);

/* Launcher [KeyMap] DisplayPerf uses SDL keycodes with Ctrl/Alt/Shift.
 * Missing binding defaults to F; an empty binding disables the hotkey. */
typedef struct FZeroFpsHotkey {
    uint32_t key;
    uint16_t modifiers;
} FZeroFpsHotkey;
FZeroFpsHotkey FZeroFpsHotkeyLoad(const char *path);

/* Copy an old save into the new location only when no destination exists. */
void FZeroMigrateLegacySave(const char *legacy_path);
