/* Host settings and keyboard bindings shared by the launcher and runtime UI. */
#include "config.h"
#include "desktop/sdl_compat.h"
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

static int FZeroStrEqNoCase(const char *a, const char *b) {
    return SDL_strcasecmp(a, b) == 0;
}

static char *FZeroTrim(char *s) {
    while (*s == ' ' || *s == '\t') ++s;
    size_t n = strlen(s);
    while (n && strchr(" \t\r\n", s[n - 1])) s[--n] = 0;
    return s;
}

static const struct SettingField {
    const char *name;
    size_t offset;
    int min, max;
} kFields[] = {
#define FIELD(name, member, min, max) {name, offsetof(FZeroSettings, member), min, max}
    FIELD("OutputMethod", output_method, 0, 0),
    FIELD("WindowScale", window_scale, 1, 8),
    FIELD("Fullscreen", fullscreen, 0, 1),
    FIELD("IgnoreAspect", ignore_aspect, 0, 1),
    FIELD("LinearFilter", linear_filter, 0, 1),
    FIELD("ShowFPS", show_fps, 0, 1),
    FIELD("Widescreen", widescreen, 0, 1),
    FIELD("VisualStyle", visual_style, 0, 3),
    FIELD("EnableAudio", enable_audio, 0, 1),
    FIELD("AudioFreq", audio_freq, 32000, 96000),
    FIELD("Volume", volume, 0, 100),
    FIELD("Player1Source", player_src[0], 0, 2),
    FIELD("Player2Source", player_src[1], 0, 0),
    FIELD("Deadzone1", deadzone[0], 0, 100),
    FIELD("Deadzone2", deadzone[1], 0, 100),
    FIELD("SkipLauncher", skip_launcher, 0, 1),
#undef FIELD
};
#define FIELD_COUNT (sizeof(kFields) / sizeof(kFields[0]))

void FZeroSettingsSanitize(FZeroSettings *s) {
    for (size_t i = 0; i < FIELD_COUNT; ++i) {
        int *v = (int *)((char *)s + kFields[i].offset);
        if (*v < kFields[i].min) *v = kFields[i].min;
        if (*v > kFields[i].max) *v = kFields[i].max;
    }
    if (s->audio_freq != 32000 && s->audio_freq != 32040 &&
        s->audio_freq != 44100 && s->audio_freq != 48000 &&
        s->audio_freq != 96000) s->audio_freq = 48000;
}

static int FieldIndex(char *line) {
    char *eq = strchr(line, '=');
    if (!eq || line[0] == '#' || line[0] == ';') return -1;
    *eq = 0;
    char *name = FZeroTrim(line);
    for (size_t i = 0; i < FIELD_COUNT; ++i)
        if (FZeroStrEqNoCase(name, kFields[i].name)) return (int)i;
    return -1;
}

static int IsSettings(char *line) {
    char *end = strchr(line, ']');
    if (!end) return 0;
    *end = 0;
    return FZeroStrEqNoCase(FZeroTrim(line + 1), "Settings");
}

void FZeroSettingsLoad(const char *path, FZeroSettings *s) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[4096];
    int in_settings = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = FZeroTrim(line);
        if (*p == '[') { in_settings = IsSettings(p); continue; }
        if (!in_settings) continue;
        char *eq = strchr(p, '=');
        int i = FieldIndex(p);
        if (i < 0) continue;
        errno = 0;
        char *end;
        long v = strtol(eq + 1, &end, 10);
        if (end == eq + 1 || errno || v < INT_MIN || v > INT_MAX) continue;
        end = FZeroTrim(end);
        if (*end && *end != '#' && *end != ';') continue;
        *(int *)((char *)s + kFields[i].offset) = (int)v;
    }
    fclose(f);
    FZeroSettingsSanitize(s);
}

static void WriteField(FILE *f, size_t i, const FZeroSettings *s) {
    fprintf(f, "%s = %d\n", kFields[i].name,
            *(const int *)((const char *)s + kFields[i].offset));
}

void FZeroSettingsSave(const char *path, const FZeroSettings *s) {
    char *tmp = malloc(strlen(path) + 5);
    if (!tmp) return;
    sprintf(tmp, "%s.tmp", path);
    FILE *out = fopen(tmp, "w");
    if (!out) { free(tmp); return; }
    FILE *in = fopen(path, "r");
    int seen[FIELD_COUNT] = {0};
    int in_settings = 0, found_settings = 0, ok = 1;
    char raw[4096], copy[4096];
    if (in) {
        while (fgets(raw, sizeof(raw), in)) {
            /* Reject overlong lines rather than silently rewrite fragments. */
            if (!strchr(raw, '\n') && !feof(in)) {
                int ch = fgetc(in);
                if (ch != EOF) { ok = 0; break; }
            }
            strcpy(copy, raw);
            char *p = FZeroTrim(copy);
            if (*p == '[') {
                if (in_settings) {
                    for (size_t i = 0; i < FIELD_COUNT; ++i)
                        if (!seen[i]) { WriteField(out, i, s); seen[i] = 1; }
                }
                in_settings = IsSettings(p);
                found_settings |= in_settings;
            } else if (in_settings) {
                int i = FieldIndex(p);
                if (i >= 0) {
                    if (!seen[i]) WriteField(out, (size_t)i, s);
                    seen[i] = 1;
                    continue;
                }
            }
            fputs(raw, out); /* Preserve unrelated text exactly. */
            if (*raw && raw[strlen(raw) - 1] != '\n') fputc('\n', out);
        }
        if (ferror(in)) ok = 0;
        fclose(in);
    } else if (errno != ENOENT) ok = 0;
    if (!found_settings) fputs("\n[Settings]\n", out);
    for (size_t i = 0; i < FIELD_COUNT; ++i)
        if (!seen[i]) WriteField(out, i, s);
    if (ferror(out)) ok = 0;
    if (fclose(out)) ok = 0;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
        ok = rename(tmp, path) == 0;
#endif
    }
    if (!ok) { fprintf(stderr, "[Config] Could not save %s\n", path); remove(tmp); }
    free(tmp);
}

void FZeroMigrateLegacySave(const char *legacy_path) {
    FILE *in = fopen(legacy_path, "rb");
    if (!in) return;
    FILE *existing = fopen("saves/save.srm", "rb");
    if (existing) { fclose(existing); fclose(in); return; }
#ifdef _WIN32
    _mkdir("saves");
#else
    mkdir("saves", 0700);
#endif
    FILE *out = fopen("saves/save.srm", "wbx");
    if (!out) { fclose(in); return; }
    char buf[4096];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), in)) != 0)
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    if (ferror(in)) ok = 0;
    fclose(in);
    if (fclose(out)) ok = 0;
    if (!ok) remove("saves/save.srm");
    else fprintf(stderr, "[Save] Copied existing save from %s\n", legacy_path);
}

typedef struct KeyBindKV {
    const char *name;
    uint32_t    bit;  /* runner 12-bit input word bit */
} KeyBindKV;

static const KeyBindKV kKeyBindNames[] = {
    {"a",      0x100},  /* A (right face) */
    {"b",      0x001},  /* B (bottom face) */
    {"x",      0x200},  /* X (top face) */
    {"y",      0x002},  /* Y (left face) */
    {"l",      0x400},
    {"r",      0x800},
    {"start",  0x008},
    {"select", 0x004},
    {"up",     0x010},
    {"down",   0x020},
    {"left",   0x040},
    {"right",  0x080},
};

void FZeroKeyBindsDefaults(uint32_t *map, int n) {
    /* Defaults match the pre-launcher host keyboard layout. */
    static const struct { SDL_Scancode sc; uint32_t bit; } d[] = {
        {SDL_SCANCODE_Z,       0x001},  /* B */
        {SDL_SCANCODE_X,       0x100},  /* A */
        {SDL_SCANCODE_A,       0x002},  /* Y */
        {SDL_SCANCODE_S,       0x200},  /* X */
        {SDL_SCANCODE_Q,       0x400},  /* L */
        {SDL_SCANCODE_E,       0x800},  /* R */
        {SDL_SCANCODE_RETURN,  0x008},  /* Start */
        {SDL_SCANCODE_BACKSPACE, 0x004},/* Select */
        {SDL_SCANCODE_UP,      0x010},
        {SDL_SCANCODE_DOWN,    0x020},
        {SDL_SCANCODE_LEFT,    0x040},
        {SDL_SCANCODE_RIGHT,   0x080},
    };
    memset(map, 0, (size_t)n * sizeof(map[0]));
    for (size_t i = 0; i < sizeof(d) / sizeof(d[0]); i++)
        if ((int)d[i].sc >= 0 && (int)d[i].sc < n)
            map[(int)d[i].sc] = d[i].bit;
}

void FZeroKeyBindsWriteDefaults(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f,
        "# Controller Keybinds\n"
        "# Edit values to customize, or rebind from the launcher's Controller\n"
        "# page (both rewrite this file). Restart the game to apply.\n"
        "# Use SDL key names. Common: A B C ... Z, 0-9, F1-F12, Up Down Left\n"
        "# Right, Return, Tab, Space, Backspace, Escape. Use \"None\" to leave\n"
        "# a button unbound.\n"
        "\n"
        "[player1]\n");
    for (size_t i = 0; i < sizeof(kKeyBindNames) / sizeof(kKeyBindNames[0]); i++) {
        /* Map the canonical button to its default scancode by name so the
         * file reads like the launcher's own output. */
        uint32_t bit = kKeyBindNames[i].bit;
        const char *name = NULL;
        static const struct { uint32_t bit; SDL_Scancode sc; } d[] = {
            {0x001, SDL_SCANCODE_Z}, {0x100, SDL_SCANCODE_X},
            {0x002, SDL_SCANCODE_A}, {0x200, SDL_SCANCODE_S},
            {0x400, SDL_SCANCODE_Q}, {0x800, SDL_SCANCODE_E},
            {0x008, SDL_SCANCODE_RETURN}, {0x004, SDL_SCANCODE_BACKSPACE},
            {0x010, SDL_SCANCODE_UP}, {0x020, SDL_SCANCODE_DOWN},
            {0x040, SDL_SCANCODE_LEFT}, {0x080, SDL_SCANCODE_RIGHT},
        };
        for (size_t j = 0; j < sizeof(d) / sizeof(d[0]); j++)
            if (d[j].bit == bit) { name = SDL_GetScancodeName(d[j].sc); break; }
        fprintf(f, "%-7s = %s\n", kKeyBindNames[i].name,
                (name && name[0]) ? name : "None");
    }
    fprintf(f, "\n[player2]\n");
    for (size_t i = 0; i < sizeof(kKeyBindNames) / sizeof(kKeyBindNames[0]); i++)
        fprintf(f, "%-7s = None\n", kKeyBindNames[i].name);
    fclose(f);
}

void FZeroKeyBindsLoad(const char *path, uint32_t *map, int n) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    int in_p1 = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = FZeroTrim(line);
        if (!p[0] || p[0] == '#' || p[0] == ';') continue;
        if (p[0] == '[') {
            char *end = strchr(p, ']');
            if (end) *end = '\0';
            in_p1 = FZeroStrEqNoCase(p + 1, "player1");
            continue;
        }
        if (!in_p1) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = FZeroTrim(p);
        char *val = FZeroTrim(eq + 1);
        SDL_Scancode sc = SDL_GetScancodeFromName(val);
        for (size_t i = 0; i < sizeof(kKeyBindNames) / sizeof(kKeyBindNames[0]); ++i) {
            if (!FZeroStrEqNoCase(key, kKeyBindNames[i].name)) continue;
            if (sc == SDL_SCANCODE_UNKNOWN && !FZeroStrEqNoCase(val, "None")) break;
            uint32_t bit = kKeyBindNames[i].bit;
            for (int j = 0; j < n; ++j) map[j] &= ~bit;
            if (sc != SDL_SCANCODE_UNKNOWN && (int)sc < n) map[(int)sc] |= bit;
            break;
        }
    }
    fclose(f);
}

FZeroFpsHotkey FZeroFpsHotkeyLoad(const char *path) {
    FZeroFpsHotkey binding = {SDLK_F, 0};
    FILE *f = fopen(path, "r");
    if (!f) return binding;
    char line[4096];
    int in_keymap = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = FZeroTrim(line);
        if (*p == '#' || *p == ';') continue;
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end) *end = 0;
            in_keymap = FZeroStrEqNoCase(FZeroTrim(p + 1), "KeyMap");
            continue;
        }
        if (!in_keymap) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        if (!FZeroStrEqNoCase(FZeroTrim(p), "DisplayPerf")) continue;
        p = eq + 1;
        char *comment = strpbrk(p, "#;");
        if (comment) *comment = 0;
        p = FZeroTrim(p);
        binding.key = 0;
        binding.modifiers = 0;
        /* Consume modifier prefixes only: '+' can be part of a key name. */
        for (;;) {
            if (!SDL_strncasecmp(p, "Ctrl+", 5)) {
                binding.modifiers |= SDL_KMOD_CTRL; p += 5;
            } else if (!SDL_strncasecmp(p, "Alt+", 4)) {
                binding.modifiers |= SDL_KMOD_ALT; p += 4;
            } else if (!SDL_strncasecmp(p, "Shift+", 6)) {
                binding.modifiers |= SDL_KMOD_SHIFT; p += 6;
            } else break;
        }
        binding.key = SDL_GetKeyFromName(p);
    }
    fclose(f);
    return binding;
}
