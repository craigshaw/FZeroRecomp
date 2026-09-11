/*
 * runtime_ui.c - in-game runtime settings overlay (recomp-ui RecompRuntimeUi)
 * adapter for the desktop host.
 *
 * Maps the launcher-exposed settings (FZeroSettings) onto the shared
 * runtime menu model: sections/items, live-apply callbacks, and input
 * translation. Values persist through FZeroSettingsSave into the same
 * config.ini [Settings] the pre-boot launcher reads, so both surfaces agree.
 *
 * Input contract: F1 toggles the menu (gamepad: Select+Start pressed
 * together, so Start alone keeps its in-game meaning). Menu open: Escape /
 * Backspace back out (section -> close), Enter selects, arrows navigate,
 * gamepad D-pad navigates with A=accept/B=back/Start=close. While open every
 * key/button is withheld from the game; closing the menu is the only way back
 * to play (plus the Resume action). The host decides pause policy - see
 * main.c, which stops running frames while the menu is open and drops stale
 * input bits when it closes.
 *
 * This file is SDL3-only in the same sense as the rest of the desktop host's
 * render path (ungated SDL3 calls in main.c); the SDL2 build falls back to
 * the pre-overlay host.
 */
#include <stdlib.h>
#include <string.h>

#include "runtime_ui.h"
#include "config.h"
#include "display_layout.h"
#include "desktop/sdl_compat.h"
#include "recomp_runtime_ui.h"

/* Same framebuffer size as src/main.c. */
#define SNES_W FZERO_NATIVE_WIDTH
#define SNES_H FZERO_DISPLAY_HEIGHT

/* Title-specific semantic keys (the standard RECOMP_RUNTIME_UI_KEY_* values
 * are used where the shared catalog already has a matching setting). */
#define FZERO_KEY_WIDESCREEN "graphics.widescreen"
#define FZERO_KEY_STRETCH       "graphics.stretch_to_fill"
#define FZERO_KEY_VISUAL_STYLE  "graphics.visual_style"
#define FZERO_KEY_SHOW_FPS      "graphics.show_fps"
#define FZERO_KEY_P1_SOURCE     "input.player1_source"
#define FZERO_KEY_P1_DEADZONE   "input.player1_deadzone"
#define FZERO_KEY_SKIP_LAUNCHER "system.skip_launcher"
#define FZERO_KEY_SCREENSHOT "system.screenshot"

struct FZeroRuntimeUi {
    FZeroSettings *settings;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_AudioStream *audio_stream;
    RecompRuntimeUi *ui;
    FZeroFpsHotkey fps_hotkey;
    int enhanced_available;
    int screenshot_requested;
    /* Physical gamepad state (menu closed) so Select+Start - not Start alone
     * - can be the open chord without stealing Start from the game. */
    int pad_start_down;
    int pad_select_down;
};

/* SDL3 renamed the controller EVENT constants (SDL_EVENT_CONTROLLER_* ->
 * SDL_EVENT_GAMEPAD_*); SDL2 only has the old names. The button/axis enums
 * are still provided under old-name aliases by both. Mirrors main.c. */
#if SNESRECOMP_SDL3
#define FZERO_EVENT_SCANCODE(ev)   ((ev).key.scancode)
#define FZERO_EVENT_BUTTON_DOWN    SDL_EVENT_GAMEPAD_BUTTON_DOWN
#define FZERO_EVENT_BUTTON_UP      SDL_EVENT_GAMEPAD_BUTTON_UP
#define FZERO_EVENT_AXIS_MOTION    SDL_EVENT_GAMEPAD_AXIS_MOTION
#else
#define FZERO_EVENT_SCANCODE(ev)   ((ev).key.keysym.scancode)
#define FZERO_EVENT_BUTTON_DOWN    SDL_EVENT_CONTROLLER_BUTTON_DOWN
#define FZERO_EVENT_BUTTON_UP      SDL_EVENT_CONTROLLER_BUTTON_UP
#define FZERO_EVENT_AXIS_MOTION    SDL_EVENT_CONTROLLER_AXIS_MOTION
#endif

/* ── menu model ──────────────────────────────────────────────────────────── */

static const char *const kPlayerSourceChoices[] = {"None", "Keyboard", "Gamepad"};
static const int kPlayerSourceValues[] = {0, 1, 2};
static const char *const kVisualChoices[] = {"Original", "Enhanced", "Vivid", "Black & White"};
static const int kVisualValues[] = {0, 1, 2, 3};

static const RecompRuntimeUiItem kItems[] = {
    /* Display */
    {RECOMP_RUNTIME_UI_KEY_FULLSCREEN, "Display", "Fullscreen",
     "Toggle borderless fullscreen.", RECOMP_RUNTIME_UI_BOOL,
     0, 1, 1, NULL, 0, NULL},
    {RECOMP_RUNTIME_UI_KEY_WINDOW_SCALE, "Display", "Window Scale",
     "Window size multiplier; applies immediately.", RECOMP_RUNTIME_UI_INT,
     1, 8, 1, NULL, 0, NULL},
    {FZERO_KEY_STRETCH, "Display", "Stretch to Fill",
     "Fill the window instead of using integer scaling.",
     RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL},
    {RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER, "Display", "Linear Filter",
     "Bilinear upscale of the framebuffer.", RECOMP_RUNTIME_UI_BOOL,
     0, 1, 1, NULL, 0, NULL},
    {FZERO_KEY_SHOW_FPS, "Display", "FPS Readout",
     "Presented frames per second.",
     RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL},
    {FZERO_KEY_VISUAL_STYLE, "Display", "Visual Style",
     "Original, soft bloom, vivid colour, or black and white.",
     RECOMP_RUNTIME_UI_CHOICE, 0, 3, 1, kVisualChoices, 4, kVisualValues},
    {FZERO_KEY_WIDESCREEN, "Display", "Widescreen",
     "Extended racing view with additional vehicles.", RECOMP_RUNTIME_UI_BOOL,
     0, 1, 1, NULL, 0, NULL},
    /* Audio */
    {RECOMP_RUNTIME_UI_KEY_VOLUME, "Audio", "Volume",
     "Master volume, percent.", RECOMP_RUNTIME_UI_INT,
     0, 100, 5, NULL, 0, NULL},
    /* Input */
    {FZERO_KEY_P1_SOURCE, "Input", "Player 1 Source",
     "None, keyboard, or gamepad for player 1.", RECOMP_RUNTIME_UI_CHOICE,
     0, 2, 1, kPlayerSourceChoices, 3, kPlayerSourceValues},
    {FZERO_KEY_P1_DEADZONE, "Input", "Stick Deadzone",
     "Gamepad stick deadzone, percent.", RECOMP_RUNTIME_UI_INT,
     0, 100, 5, NULL, 0, NULL},
    /* System */
    {FZERO_KEY_SKIP_LAUNCHER, "System", "Skip Launcher",
     "Boot straight into the game next launch from the cached ROM.",
     RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL},
    {RECOMP_RUNTIME_UI_KEY_RESUME, "System", "Resume",
     "Close the menu and keep playing.", RECOMP_RUNTIME_UI_ACTION,
     0, 0, 0, NULL, 0, NULL},
    {FZERO_KEY_SCREENSHOT, "System", "Take Screenshot (F12)",
     "Save the game image to the screenshots folder.", RECOMP_RUNTIME_UI_ACTION,
     0, 0, 0, NULL, 0, NULL},
};

/* ── callbacks (live-apply + persist) ───────────────────────────────────── */

static int GetValue(void *context, const RecompRuntimeUiItem *item,
                    int *value_out) {
  FZeroRuntimeUi *rt = (FZeroRuntimeUi *)context;
  const FZeroSettings *s = rt->settings;
  if (!strcmp(item->key, RECOMP_RUNTIME_UI_KEY_FULLSCREEN))
    *value_out = s->fullscreen != 0;
  else if (!strcmp(item->key, RECOMP_RUNTIME_UI_KEY_WINDOW_SCALE))
    *value_out = s->window_scale;
  else if (!strcmp(item->key, FZERO_KEY_STRETCH))
    *value_out = s->ignore_aspect;
  else if (!strcmp(item->key, RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER))
    *value_out = s->linear_filter;
  else if (!strcmp(item->key, RECOMP_RUNTIME_UI_KEY_VOLUME))
    *value_out = s->volume;
  else if (!strcmp(item->key, FZERO_KEY_WIDESCREEN))
    *value_out = s->widescreen;
  else if (!strcmp(item->key, FZERO_KEY_SHOW_FPS))
    *value_out = s->show_fps;
  else if (!strcmp(item->key, FZERO_KEY_VISUAL_STYLE))
    *value_out = rt->enhanced_available ? s->visual_style : 0;
  else if (!strcmp(item->key, FZERO_KEY_P1_SOURCE))
    *value_out = s->player_src[0];
  else if (!strcmp(item->key, FZERO_KEY_P1_DEADZONE))
    *value_out = s->deadzone[0];
  else if (!strcmp(item->key, FZERO_KEY_SKIP_LAUNCHER))
    *value_out = s->skip_launcher;
  else
    return 0;
  return 1;
}

static int SetValue(void *context, const RecompRuntimeUiItem *item,
                    int value) {
  FZeroRuntimeUi *rt = (FZeroRuntimeUi *)context;
  FZeroSettings *s = rt->settings;
  if (!strcmp(item->key, RECOMP_RUNTIME_UI_KEY_FULLSCREEN)) {
    s->fullscreen = value != 0;
    snesrecomp_sdl_set_fullscreen(rt->window, s->fullscreen);
  } else if (!strcmp(item->key, RECOMP_RUNTIME_UI_KEY_WINDOW_SCALE)) {
    s->window_scale = value < 1 ? 1 : (value > 8 ? 8 : value);
    SDL_SetWindowSize(rt->window, FZeroDisplayWidth(s->widescreen) * s->window_scale,
                      SNES_H * s->window_scale);
  } else if (!strcmp(item->key, FZERO_KEY_STRETCH)) {
    s->ignore_aspect = value != 0;
    FZeroRuntimeUiReapplyLogicalPresentation(rt->renderer, s);
  } else if (!strcmp(item->key, RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER)) {
    s->linear_filter = value != 0;
    snesrecomp_sdl_set_texture_linear(rt->texture, s->linear_filter);
  } else if (!strcmp(item->key, RECOMP_RUNTIME_UI_KEY_VOLUME)) {
    s->volume = value < 0 ? 0 : (value > 100 ? 100 : value);
    if (rt->audio_stream)
      SDL_SetAudioStreamGain(rt->audio_stream, (float)s->volume / 100.0f);
  } else if (!strcmp(item->key, FZERO_KEY_WIDESCREEN)) {
    s->widescreen = value != 0;
    if (!(SDL_GetWindowFlags(rt->window) & SDL_WINDOW_FULLSCREEN))
      SDL_SetWindowSize(rt->window, FZeroDisplayWidth(s->widescreen) * s->window_scale,
                       SNES_H * s->window_scale);
    FZeroRuntimeUiReapplyLogicalPresentation(rt->renderer, s);
  } else if (!strcmp(item->key, FZERO_KEY_SHOW_FPS)) {
    s->show_fps = value != 0;
  } else if (!strcmp(item->key, FZERO_KEY_VISUAL_STYLE)) {
    if (!rt->enhanced_available) return 0;
    s->visual_style = value < 0 ? 0 : (value > 3 ? 3 : value);
  } else if (!strcmp(item->key, FZERO_KEY_P1_SOURCE)) {
    /* Applied live by main.c, which syncs the gamepad each loop iteration. */
    s->player_src[0] = value;
  } else if (!strcmp(item->key, FZERO_KEY_P1_DEADZONE)) {
    /* Read live by ApplyStickToDpad in main.c. */
    s->deadzone[0] = value < 0 ? 0 : (value > 100 ? 100 : value);
  } else if (!strcmp(item->key, FZERO_KEY_SKIP_LAUNCHER)) {
    s->skip_launcher = value != 0;
  } else {
    return 0;
  }
  return 1;
}

static int RunAction(void *context, const RecompRuntimeUiItem *item) {
  FZeroRuntimeUi *rt = (FZeroRuntimeUi *)context;
  if (!strcmp(item->key, FZERO_KEY_SCREENSHOT)) {
    rt->screenshot_requested = 1;
    return 1;
  }
  if (!strcmp(item->key, RECOMP_RUNTIME_UI_KEY_RESUME)) {
    recomp_runtime_ui_close(rt->ui);
    return 1;
  }
  return 0;
}

static int IsEnabled(void *context, const RecompRuntimeUiItem *item) {
  FZeroRuntimeUi *rt = (FZeroRuntimeUi *)context;
  if (!strcmp(item->key, FZERO_KEY_VISUAL_STYLE))
    return rt->enhanced_available;
  if (!strcmp(item->key, FZERO_KEY_P1_DEADZONE))
    return rt->settings->player_src[0] == 2;
  return 1;
}

static void Save(void *context) {
  FZeroRuntimeUi *rt = (FZeroRuntimeUi *)context;
  /* Same file the pre-boot launcher and its hotkey editor write; the surgical
   * in-place update preserves the launcher's [KeyMap] section. */
  FZeroSettingsSave("config.ini", rt->settings);
}

static void VisibilityChanged(void *context, int open) {
  FZeroRuntimeUi *rt = (FZeroRuntimeUi *)context;
  /* The host stops running frames while the menu is open, so nothing feeds
   * the audio stream; pause the device to stop the pull (clean silence
   * instead of underrun churn) and resume on close. */
  if (rt->audio_stream) {
    if (open)
      SDL_PauseAudioStreamDevice(rt->audio_stream);
    else
      SDL_ResumeAudioStreamDevice(rt->audio_stream);
  }
}

/* ── public API ─────────────────────────────────────────────────────────── */

FZeroRuntimeUi *FZeroRuntimeUiCreate(
    FZeroSettings *settings, SDL_Window *window, SDL_Renderer *renderer,
    SDL_Texture *texture, SDL_AudioStream *audio_stream) {
  FZeroRuntimeUi *rt = (FZeroRuntimeUi *)calloc(1, sizeof(*rt));
  if (!rt) return NULL;
  rt->settings = settings;
  rt->fps_hotkey = FZeroFpsHotkeyLoad("config.ini");
  rt->window = window;
  rt->renderer = renderer;
  rt->texture = texture;
  rt->audio_stream = audio_stream;

  RecompRuntimeUiConfig config;
  memset(&config, 0, sizeof(config));
  config.title = "F-Zero";
  config.subtitle = "Runtime Settings";
  config.items = kItems;
  config.item_count = sizeof(kItems) / sizeof(kItems[0]);
  config.callbacks.context = rt;
  config.callbacks.get_value = GetValue;
  config.callbacks.set_value = SetValue;
  config.callbacks.run_action = RunAction;
  config.callbacks.is_enabled = IsEnabled;
  config.callbacks.save = Save;
  config.callbacks.visibility_changed = VisibilityChanged;
  config.theme = "snes";
  config.accept_label = "Enter";
  config.back_label = "Backspace";

  rt->ui = recomp_runtime_ui_create(&config);
  if (!rt->ui) {
    free(rt);
    return NULL;
  }
  return rt;
}

void FZeroRuntimeUiSetEnhancedAvailable(FZeroRuntimeUi *rt, int available) {
  if (rt) rt->enhanced_available = available != 0;
}

void FZeroRuntimeUiDestroy(FZeroRuntimeUi *rt) {
  if (!rt) return;
  if (rt->ui) recomp_runtime_ui_destroy(rt->ui);
  free(rt);
}

int FZeroRuntimeUiIsOpen(const FZeroRuntimeUi *rt) {
  return rt && rt->ui && recomp_runtime_ui_is_open(rt->ui);
}

void FZeroRuntimeUiOpen(FZeroRuntimeUi *rt) {
  if (rt && rt->ui) recomp_runtime_ui_open(rt->ui);
}

int FZeroRuntimeUiHandleEvent(FZeroRuntimeUi *rt,
                                    const SDL_Event *event) {
  if (!rt || !rt->ui) return 0;
  RecompRuntimeUi *ui = rt->ui;
  const int open = recomp_runtime_ui_is_open(ui);
  RecompRuntimeUiInput in = (RecompRuntimeUiInput)-1;
  int pressed = 0;
  int repeat = 0;

  /* F12 captures the game even while paused in settings. Key-up and repeats
   * are consumed without taking additional screenshots. */
  if ((event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) &&
      event->key.scancode == SDL_SCANCODE_F12) {
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
        !(event->key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT | SDL_KMOD_GUI)))
      rt->screenshot_requested = 1;
    return 1;
  }

  /* Menu controls retain priority while open; F1 always owns the menu.
   * Ignore lock bits, and accept either side of each required modifier. */
  if (!open && (event->type == SDL_EVENT_KEY_DOWN ||
                event->type == SDL_EVENT_KEY_UP) &&
      event->key.scancode != SDL_SCANCODE_F1 && rt->fps_hotkey.key &&
      event->key.key == rt->fps_hotkey.key) {
    SDL_Keymod mods = event->key.mod;
    uint16_t normalized = ((mods & SDL_KMOD_CTRL) ? SDL_KMOD_CTRL : 0) |
                          ((mods & SDL_KMOD_ALT) ? SDL_KMOD_ALT : 0) |
                          ((mods & SDL_KMOD_SHIFT) ? SDL_KMOD_SHIFT : 0);
    if (normalized == rt->fps_hotkey.modifiers && !(mods & SDL_KMOD_GUI)) {
      if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        rt->settings->show_fps ^= 1;
        Save(rt);
      }
      return 1;
    }
  }

  switch (event->type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    pressed = event->type == SDL_EVENT_KEY_DOWN;
    repeat = pressed && event->key.repeat;
    switch (FZERO_EVENT_SCANCODE(*event)) {
    case SDL_SCANCODE_F1: in = RECOMP_RUNTIME_UI_INPUT_TOGGLE; break;
    case SDL_SCANCODE_ESCAPE: in = RECOMP_RUNTIME_UI_INPUT_BACK; break;
    case SDL_SCANCODE_RETURN: in = RECOMP_RUNTIME_UI_INPUT_ACCEPT; break;
    case SDL_SCANCODE_BACKSPACE: in = RECOMP_RUNTIME_UI_INPUT_BACK; break;
    case SDL_SCANCODE_UP: in = RECOMP_RUNTIME_UI_INPUT_UP; break;
    case SDL_SCANCODE_DOWN: in = RECOMP_RUNTIME_UI_INPUT_DOWN; break;
    case SDL_SCANCODE_LEFT: in = RECOMP_RUNTIME_UI_INPUT_LEFT; break;
    case SDL_SCANCODE_RIGHT: in = RECOMP_RUNTIME_UI_INPUT_RIGHT; break;
    default:
      /* Unknown key: withheld while the menu is open; the game sees it when
       * the menu is closed (the game never binds F1, so no clash). */
      return open ? 1 : 0;
    }
    break;
  case FZERO_EVENT_BUTTON_DOWN:
  case FZERO_EVENT_BUTTON_UP: {
    pressed = event->type == FZERO_EVENT_BUTTON_DOWN;
    const int button = SNESRECOMP_SDL_EVENT_BUTTON(*event);
    /* Track the physical pad state regardless of the menu, so the closed
     * menu can detect the Select+Start open chord below. */
    if (button == SDL_CONTROLLER_BUTTON_START)
      rt->pad_start_down = pressed;
    else if (button == SDL_CONTROLLER_BUTTON_BACK) /* the Select button */
      rt->pad_select_down = pressed;
    if (!open) {
      /* Menu closed: only the Select+Start chord opens it; every other pad
       * input - including Start alone, the game's pause/menu button - is the
       * game's. */
      if (pressed && !repeat &&
          (button == SDL_CONTROLLER_BUTTON_START ||
           button == SDL_CONTROLLER_BUTTON_BACK) &&
          rt->pad_select_down && rt->pad_start_down) {
        return recomp_runtime_ui_handle_input(
                   ui, RECOMP_RUNTIME_UI_INPUT_TOGGLE, 1, 0) != 0;
      }
      return 0;
    }
    switch (button) {
    case SDL_CONTROLLER_BUTTON_START: in = RECOMP_RUNTIME_UI_INPUT_TOGGLE; break;
    case SDL_CONTROLLER_BUTTON_A: in = RECOMP_RUNTIME_UI_INPUT_ACCEPT; break;
    case SDL_CONTROLLER_BUTTON_B: in = RECOMP_RUNTIME_UI_INPUT_BACK; break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: in = RECOMP_RUNTIME_UI_INPUT_UP; break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: in = RECOMP_RUNTIME_UI_INPUT_DOWN; break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: in = RECOMP_RUNTIME_UI_INPUT_LEFT; break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: in = RECOMP_RUNTIME_UI_INPUT_RIGHT; break;
    default:
      /* Any other pad input is withheld while the menu is open. */
      return 1;
    }
    break;
  }
  case FZERO_EVENT_AXIS_MOTION:
    /* The D-pad drives the menu; consume stick motion while open so it does
     * not leak into the (paused) game input state. */
    return open ? 1 : 0;
  default:
    return 0;
  }

  /* Route to the shared menu model. Its return value IS the consume verdict:
   * a closed menu consumes only the F1 toggle (opening it), so the game gets
   * every other key - Enter/arrows included; an open menu consumes all. */
  if ((int)in >= 0)
    return recomp_runtime_ui_handle_input(ui, in, pressed, repeat) != 0;
  return open ? 1 : 0;
}

void FZeroRuntimeUiRestoreRendererState(FZeroRuntimeUi *rt,
                                              SDL_Renderer *renderer) {
  if (!rt || !renderer) return;
  /* The ImGui renderer backend restores the viewport to the concrete rect it
   * captured while the logical presentation was temporarily disabled. If that
   * rect survived, re-enabling the 256x224 presentation would reinterpret it
   * in logical coordinates (viewport coords are logical-space in SDL3), giving
   * a many-times-larger viewport anchored top-left - the picture "zooms into
   * the top-left corner". Reset the viewport to the scale-proof full-target
   * state first, then re-enable the logical presentation. Draw commands
   * already queued (game + overlay) keep the scale they were queued with;
   * only the state for the next frame changes here. */
  SDL_SetRenderViewport(renderer, NULL);
  FZeroRuntimeUiReapplyLogicalPresentation(renderer, rt->settings);
}

void FZeroRuntimeUiReapplyLogicalPresentation(
    SDL_Renderer *renderer, const FZeroSettings *settings) {
  SDL_SetRenderLogicalPresentation(
      renderer, FZeroDisplayWidth(settings->widescreen), SNES_H,
      settings->ignore_aspect ? SDL_LOGICAL_PRESENTATION_STRETCH
                              : SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
}

RecompRuntimeUi *FZeroRuntimeUiCore(const FZeroRuntimeUi *rt) {
  return rt ? rt->ui : NULL;
}

int FZeroRuntimeUiTakeScreenshotRequest(FZeroRuntimeUi *rt) {
  if (!rt) return 0;
  int requested = rt->screenshot_requested;
  rt->screenshot_requested = 0;
  return requested;
}

void FZeroRuntimeUiResetPad(FZeroRuntimeUi *rt) {
  if (rt) rt->pad_start_down = rt->pad_select_down = 0;
}
