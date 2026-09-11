/* Synthetic host/UI contract tests. No cartridge data or title recordings. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "config.h"
#include "frame_rate.h"
#include "runtime_ui.h"
#include "runtime_ui_imgui.h"
#include "presentation.h"

#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s failed (%s)\n", __FILE__, __LINE__, #expr, SDL_GetError()); \
  exit(1); } } while (0)

static void Write(const char *path, const char *text) {
  FILE *f = fopen(path, "wb"); CHECK(f);
  CHECK(fwrite(text, 1, strlen(text), f) == strlen(text));
  CHECK(fclose(f) == 0);
}
static char *Read(const char *path) {
  FILE *f = fopen(path, "rb"); CHECK(f);
  CHECK(fseek(f, 0, SEEK_END) == 0);
  long size = ftell(f); CHECK(size >= 0);
  rewind(f);
  char *text = calloc((size_t)size + 1, 1); CHECK(text);
  CHECK(fread(text, 1, (size_t)size, f) == (size_t)size);
  fclose(f); return text;
}
static void TestSettings(void) {
  const char *other = "[KeyMap]\n  # Keep this comment and its spaces.\nPause = P\n";
  Write("config.ini", "# user file\n[Settings]\nVolume = 20\nVolume = 99\n"
        "CustomKey = retained\n[KeyMap]\n  # Keep this comment and its spaces.\nPause = P\n");
  FZeroSettings s; FZeroSettingsInitDefault(&s);
  s.widescreen = 1; s.volume = 35; s.window_scale = 4; s.linear_filter = 1; s.show_fps = 1; s.visual_style = 3;
  FZeroSettingsSave("config.ini", &s);
  char *text = Read("config.ini");
  CHECK(strstr(text, other)); CHECK(strstr(text, "CustomKey = retained\n"));
  CHECK(!strstr(text, "Volume = 99")); free(text);
  FZeroSettings loaded; FZeroSettingsInitDefault(&loaded);
  FZeroSettingsLoad("config.ini", &loaded);
  CHECK(loaded.volume == 35 && loaded.window_scale == 4 && loaded.linear_filter == 1);
  CHECK(loaded.show_fps == 1 && loaded.visual_style == 3 && loaded.widescreen == 1);
  const char *windows_other = "[KeyMap]\r\n# Windows line endings\r\nPause = P\r\n";
  Write("windows.ini", "[Settings]\r\nVolume = 20\r\n[KeyMap]\r\n# Windows line endings\r\nPause = P\r\n");
  FZeroSettingsSave("windows.ini", &s);
  text = Read("windows.ini"); CHECK(strstr(text, windows_other)); free(text);
  Write("invalid.ini", "[Settings]\nVolume = -99\nWindowScale = 9000\n"
        "AudioFreq = 12345\nPlayer1Source = 10\nDeadzone1 = -5\n"
        "LinearFilter = junk\nSkipLauncher = 999999999999999999999999999999\n");
  FZeroSettingsInitDefault(&loaded); FZeroSettingsLoad("invalid.ini", &loaded);
  CHECK(loaded.volume == 0 && loaded.window_scale == 8);
  CHECK(loaded.audio_freq == 32000 && loaded.player_src[0] == 2);
  CHECK(loaded.deadzone[0] == 0 && loaded.skip_launcher == 0 && loaded.linear_filter == 0);
}
static void TestBindings(void) {
  uint32_t map[SDL_NUM_SCANCODES];
  FZeroKeyBindsDefaults(map, SDL_NUM_SCANCODES);
  Write("keybinds.ini", "[player1]\nb = None\na = C\nstart = C\n"
        "up = InvalidScancode\n[player2]\na = V\n");
  FZeroKeyBindsLoad("keybinds.ini", map, SDL_NUM_SCANCODES);
  CHECK(map[SDL_SCANCODE_Z] == 0 && map[SDL_SCANCODE_X] == 0);
  CHECK(map[SDL_SCANCODE_RETURN] == 0);
  CHECK(map[SDL_SCANCODE_C] == (0x100 | 0x008));
  CHECK(map[SDL_SCANCODE_V] == 0 && map[SDL_SCANCODE_UP] == 0x010);
}
static void TestSaveMigration(void) {
  remove("saves/save.srm"); /* Only this test target's isolated synthetic save. */
  Write("legacy.srm", "synthetic save data");
  FZeroMigrateLegacySave("legacy.srm");
  char *text = Read("saves/save.srm"); CHECK(!strcmp(text, "synthetic save data")); free(text);
  Write("legacy.srm", "different data");
  FZeroMigrateLegacySave("legacy.srm");
  text = Read("saves/save.srm"); CHECK(!strcmp(text, "synthetic save data")); free(text);
  text = Read("legacy.srm"); CHECK(!strcmp(text, "different data")); free(text);
}
static int Key(FZeroRuntimeUi *rt, SDL_Scancode key, int down) {
  SDL_Event e = {0}; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.scancode = key;
  e.key.key = SDL_GetKeyFromName(SDL_GetScancodeName(key));
  return FZeroRuntimeUiHandleEvent(rt, &e);
}
static int Button(FZeroRuntimeUi *rt, int button, int down) {
  SDL_Event e = {0}; e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
  e.gbutton.button = (Uint8)button;
  return FZeroRuntimeUiHandleEvent(rt, &e);
}
/* Unlike the game loop, these tests do not poll events each frame. Settle
 * native window changes and let the GPU swapchain catch up before readback. */
static void SettleWindow(SDL_Window *window, SDL_Renderer *renderer) {
  CHECK(SDL_SyncWindow(window));
  SDL_PumpEvents();
  CHECK(SDL_RenderPresent(renderer));
}

static void TestMenu(void) {
  Write("config.ini", "[KeyMap]\nDisplayPerf = F\n");
  FZeroSettings s; FZeroSettingsInitDefault(&s);
  SDL_Window *window = SDL_CreateWindow("Synthetic UI test", 768, 672, getenv("FZERO_TEST_GPU") ? 0 : SDL_WINDOW_HIDDEN);
  CHECK(window);
  /* Exercise the same renderer selection as the game, including GPU runs. */
  FZeroPresentation *presentation = FZeroPresentationCreate(window, false); CHECK(presentation);
  SDL_Renderer *renderer = FZeroPresentationRenderer(presentation); CHECK(renderer);
  SettleWindow(window, renderer);
  SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STREAMING, 256, 224); CHECK(texture);
  FZeroRuntimeUi *rt = FZeroRuntimeUiCreate(&s, window, renderer, texture, NULL); CHECK(rt);
  FZeroImGui *ig = fzero_imgui_create(window, renderer); CHECK(ig);
  CHECK(Key(rt, SDL_SCANCODE_F, 1)); CHECK(s.show_fps);
  CHECK(Key(rt, SDL_SCANCODE_F, 0)); CHECK(s.show_fps);
  SDL_Event repeat = {0}; repeat.type = SDL_EVENT_KEY_DOWN;
  repeat.key.scancode = SDL_SCANCODE_F; repeat.key.key = SDLK_F;
  repeat.key.repeat = true;
  CHECK(FZeroRuntimeUiHandleEvent(rt, &repeat)); CHECK(s.show_fps);
  FZeroSettings saved; FZeroSettingsInitDefault(&saved);
  FZeroSettingsLoad("config.ini", &saved); CHECK(saved.show_fps);
  CHECK(Key(rt, SDL_SCANCODE_F, 1)); CHECK(!s.show_fps);
  /* The counter must draw with the menu closed, then disappear completely. */
  for (int show = 1; show >= 0; --show) {
    FZeroRuntimeUiReapplyLogicalPresentation(renderer, &s);
    SDL_SetRenderDrawColor(renderer, 20, 40, 60, 255);
    SDL_RenderClear(renderer);
    fzero_imgui_render_overlay(ig, rt, renderer, show, 60.0);
    CHECK(!FZeroRuntimeUiIsOpen(rt));
    CHECK(!SDL_RenderViewportSet(renderer));
    SDL_Surface *raw = SDL_RenderReadPixels(renderer, NULL); CHECK(raw);
    SDL_Surface *pixels = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888); CHECK(pixels);
    int changed = 0;
    for (int y = 8; y < 40; ++y) {
      Uint32 *row = (Uint32 *)((Uint8 *)pixels->pixels + y * pixels->pitch);
      for (int x = 8; x < 150; ++x)
        if ((row[x] & 0xffffff) != 0x14283c) ++changed;
    }
    CHECK(show ? changed > 100 : changed == 0);
    SDL_DestroySurface(pixels); SDL_DestroySurface(raw);
    CHECK(SDL_RenderPresent(renderer));
  }
  CHECK(!Key(rt, SDL_SCANCODE_RETURN, 1));
  CHECK(!Key(rt, SDL_SCANCODE_UP, 1));
  CHECK(!Button(rt, SDL_GAMEPAD_BUTTON_START, 1));
  CHECK(!Button(rt, SDL_GAMEPAD_BUTTON_START, 0));
  CHECK(Key(rt, SDL_SCANCODE_F1, 1)); CHECK(FZeroRuntimeUiIsOpen(rt));
  CHECK(Key(rt, SDL_SCANCODE_Z, 1)); CHECK(Key(rt, SDL_SCANCODE_Z, 0));
  CHECK(Key(rt, SDL_SCANCODE_F1, 0));
  for (int i = 0; i < 3; ++i) {
    FZeroRuntimeUiReapplyLogicalPresentation(renderer, &s);
    SDL_SetRenderDrawColor(renderer, 20, 40, 60, 255);
    SDL_RenderClear(renderer);
    fzero_imgui_render_overlay(ig, rt, renderer, 1, 60.0);
    int w, h; SDL_RendererLogicalPresentation mode;
    CHECK(SDL_GetRenderLogicalPresentation(renderer, &w, &h, &mode));
    CHECK(w == 256 && h == 224 && mode == SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    CHECK(!SDL_RenderViewportSet(renderer));
    SDL_Surface *pixels = SDL_RenderReadPixels(renderer, NULL); CHECK(pixels);
    SDL_DestroySurface(pixels);
    CHECK(SDL_RenderPresent(renderer));
  }
  CHECK(Key(rt, SDL_SCANCODE_F1, 1)); CHECK(!FZeroRuntimeUiIsOpen(rt));
  /* Display menu toggle applies and persists through the normal callbacks. */
  Key(rt, SDL_SCANCODE_F1, 1);
  Key(rt, SDL_SCANCODE_RETURN, 1);
  for (int i = 0; i < 4; ++i) Key(rt, SDL_SCANCODE_DOWN, 1);
  Key(rt, SDL_SCANCODE_RIGHT, 1);
  CHECK(s.show_fps);
  FZeroSettingsLoad("config.ini", &saved); CHECK(saved.show_fps);
  Key(rt, SDL_SCANCODE_DOWN, 1);
  Key(rt, SDL_SCANCODE_RIGHT, 1); CHECK(s.visual_style == 0); /* no shader */
  FZeroRuntimeUiSetEnhancedAvailable(rt, 1);
  Key(rt, SDL_SCANCODE_RIGHT, 1); CHECK(s.visual_style == 1);
  FZeroSettingsLoad("config.ini", &saved); CHECK(saved.visual_style == 1);
  Key(rt, SDL_SCANCODE_RIGHT, 1); CHECK(s.visual_style == 2);
  FZeroSettingsLoad("config.ini", &saved); CHECK(saved.visual_style == 2);
  Key(rt, SDL_SCANCODE_RIGHT, 1); CHECK(s.visual_style == 3);
  FZeroSettingsLoad("config.ini", &saved); CHECK(saved.visual_style == 3);
  Key(rt, SDL_SCANCODE_LEFT, 1); CHECK(s.visual_style == 2);
  Key(rt, SDL_SCANCODE_LEFT, 1); CHECK(s.visual_style == 1);
  Key(rt, SDL_SCANCODE_LEFT, 1); CHECK(s.visual_style == 0);
  FZeroSettingsLoad("config.ini", &saved); CHECK(saved.visual_style == 0);
  Key(rt, SDL_SCANCODE_DOWN, 1); Key(rt, SDL_SCANCODE_RIGHT, 1); CHECK(s.widescreen);
  FZeroSettingsLoad("config.ini", &saved); CHECK(saved.widescreen);
  int wide_w,wide_h; SDL_RendererLogicalPresentation wide_mode;
  CHECK(SDL_GetRenderLogicalPresentation(renderer,&wide_w,&wide_h,&wide_mode));
  CHECK(wide_w==FZERO_WIDE_WIDTH && wide_h==224);
  SettleWindow(window, renderer);
  fzero_imgui_render_overlay(ig, rt, renderer, 1, 60.0);
  CHECK(SDL_GetRenderLogicalPresentation(renderer,&wide_w,&wide_h,&wide_mode));
  CHECK(wide_w==FZERO_WIDE_WIDTH && !SDL_RenderViewportSet(renderer));
  Key(rt, SDL_SCANCODE_LEFT, 1); CHECK(!s.widescreen);
  SettleWindow(window, renderer);
  FZeroSettingsLoad("config.ini", &saved); CHECK(!saved.widescreen);
  CHECK(SDL_GetRenderLogicalPresentation(renderer,&wide_w,&wide_h,&wide_mode)); CHECK(wide_w==256);
  Key(rt, SDL_SCANCODE_F1, 1); CHECK(!FZeroRuntimeUiIsOpen(rt));
  CHECK(!Button(rt, SDL_GAMEPAD_BUTTON_BACK, 1));
  CHECK(Button(rt, SDL_GAMEPAD_BUTTON_START, 1)); CHECK(FZeroRuntimeUiIsOpen(rt));
  Key(rt, SDL_SCANCODE_F1, 1); CHECK(!FZeroRuntimeUiIsOpen(rt));
  FZeroRuntimeUiResetPad(rt);
  CHECK(!Button(rt, SDL_GAMEPAD_BUTTON_START, 1)); CHECK(!FZeroRuntimeUiIsOpen(rt));
  fzero_imgui_destroy(ig); FZeroRuntimeUiDestroy(rt);
  SDL_DestroyTexture(texture); FZeroPresentationDestroy(presentation); SDL_DestroyWindow(window);
}
static void TestFps(void) {
  FZeroFrameRate rate; FZeroFrameRateInit(&rate, 1000000000);
  CHECK(rate.fps < 0);
  for (int i = 1; i <= 30; ++i)
    FZeroFrameRatePresent(&rate, 1000000000 + (uint64_t)i * 500000000 / 30);
  CHECK(rate.fps == 60.0);
  for (int i = 1; i <= 15; ++i)
    FZeroFrameRatePresent(&rate, 1500000000 + (uint64_t)i * 500000000 / 15);
  CHECK(rate.fps == 30.0);
  Write("fps-bind.ini", "[Settings]\nDisplayPerf = X\n");
  FZeroFpsHotkey hotkey = FZeroFpsHotkeyLoad("fps-bind.ini");
  CHECK(hotkey.key == SDLK_F && !hotkey.modifiers);
  Write("fps-bind.ini", "[KeyMap]\nDisplayPerf = Ctrl+Alt+Shift+F10 # saved by launcher\n");
  hotkey = FZeroFpsHotkeyLoad("fps-bind.ini");
  CHECK(hotkey.key == SDLK_F10);
  CHECK(hotkey.modifiers == (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_SHIFT));
  Write("config.ini", "[KeyMap]\nDisplayPerf = Ctrl+F10\n");
  FZeroSettings s; FZeroSettingsInitDefault(&s);
  FZeroRuntimeUi *rt = FZeroRuntimeUiCreate(&s, NULL, NULL, NULL, NULL); CHECK(rt);
  SDL_Event event = {0}; event.type = SDL_EVENT_KEY_DOWN;
  event.key.scancode = SDL_SCANCODE_F10; event.key.key = SDLK_F10;
  CHECK(!FZeroRuntimeUiHandleEvent(rt, &event)); CHECK(!s.show_fps);
  event.key.mod = SDL_KMOD_RCTRL | SDL_KMOD_CAPS;
  CHECK(FZeroRuntimeUiHandleEvent(rt, &event)); CHECK(s.show_fps);
  FZeroRuntimeUiDestroy(rt);
  Write("fps-bind.ini", "[KeyMap]\nDisplayPerf = \n");
  CHECK(!FZeroFpsHotkeyLoad("fps-bind.ini").key);
}

static void TestPresentation(void) {
  SDL_Window *window = SDL_CreateWindow("Synthetic presentation", 768, 672, getenv("FZERO_TEST_GPU") ? 0 : SDL_WINDOW_HIDDEN);
  CHECK(window);
  FZeroPresentation *video = FZeroPresentationCreate(window, false); CHECK(video);
  if (getenv("FZERO_TEST_GPU")) CHECK(FZeroPresentationHasShader(video));
  SDL_Renderer *renderer = FZeroPresentationRenderer(video);
  SettleWindow(window, renderer);
  SDL_Texture *texture = FZeroPresentationTexture(video);
  static Uint32 world[224][256], hud[224][256], expected[224][256];
  for (int y = 0; y < 224; ++y) for (int x = 0; x < 256; ++x) {
    world[y][x] = ((x * 17 % 256) << 16) | ((y * 13 % 256) << 8) | ((x + y) % 256);
    hud[y][x] = y < 48 && x % 7 < 3 ? 0xffffa020 : 0;
    expected[y][x] = hud[y][x] ? hud[y][x] : world[y][x];
  }
  CHECK(FZeroPresentationUpload(video, world, hud));
  SDL_Texture *reference = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STREAMING, 256, 224); CHECK(reference);
  CHECK(SDL_SetTextureBlendMode(reference, SDL_BLENDMODE_NONE));
  CHECK(SDL_UpdateTexture(reference, NULL, expected, sizeof(expected[0])));
  /* Repeated frames, both filters, and the menu exercise target and state
   * restoration. Compare the actual GPU composite with synthetic colours. */
  FZeroSettings settings; FZeroSettingsInitDefault(&settings);
  FZeroRuntimeUi *rt = FZeroRuntimeUiCreate(&settings, window, renderer, texture, NULL); CHECK(rt);
  FZeroImGui *ig = fzero_imgui_create(window, renderer); CHECK(ig);
  for (int i = 0; i < 4; ++i) {
    if (i == 2) {
      CHECK(SDL_SetWindowSize(window, 1001, 733));
      SettleWindow(window, renderer);
      settings.ignore_aspect = 1;
    }
    CHECK(SDL_SetTextureScaleMode(texture, i & 1 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST));
    CHECK(SDL_SetTextureScaleMode(reference, i & 1 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST));
    FZeroRuntimeUiReapplyLogicalPresentation(renderer, &settings);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    CHECK(SDL_RenderClear(renderer));
    CHECK(FZeroPresentationDraw(video));
    CHECK(FZeroPresentationMatches(video, expected));
    CHECK(SDL_GetRenderTarget(renderer) == NULL);
    SDL_Surface *raw = FZeroPresentationReadComposite(video); CHECK(raw);
    SDL_Surface *pixels = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888); CHECK(pixels);
    CHECK(pixels->w == 256 && pixels->h == 224);
    for (int y = 0; y < 224; ++y) {
      Uint32 *row = (Uint32 *)((Uint8 *)pixels->pixels + y * pixels->pitch);
      for (int x = 0; x < 256; ++x) CHECK((row[x] & 0xffffff) == (expected[y][x] & 0xffffff));
    }
    SDL_DestroySurface(pixels); SDL_DestroySurface(raw);
    /* Compare final scaling with the original single-texture path, including
     * non-integer stretch and linear filtering at the HUD boundary. */
    SDL_Surface *composed = SDL_RenderReadPixels(renderer, NULL); CHECK(composed);
    CHECK(SDL_RenderClear(renderer));
    CHECK(SDL_RenderTexture(renderer, reference, NULL, NULL));
    SDL_Surface *direct = SDL_RenderReadPixels(renderer, NULL); CHECK(direct);
    CHECK(composed->w == direct->w && composed->h == direct->h && composed->format == direct->format);
    SDL_Surface *a = SDL_ConvertSurface(composed, SDL_PIXELFORMAT_ARGB8888); CHECK(a);
    SDL_Surface *b = SDL_ConvertSurface(direct, SDL_PIXELFORMAT_ARGB8888); CHECK(b);
    for (int y = 0; y < b->h; ++y) {
      Uint32 *arow = (Uint32 *)((Uint8 *)a->pixels + y * a->pitch);
      Uint32 *brow = (Uint32 *)((Uint8 *)b->pixels + y * b->pitch);
      for (int x = 0; x < b->w; ++x) {
        if ((arow[x] & 0xffffff) != (brow[x] & 0xffffff))
          fprintf(stderr, "scaled frame %d at %d,%d: %08x != %08x\n", i, x, y, arow[x], brow[x]);
        CHECK((arow[x] & 0xffffff) == (brow[x] & 0xffffff));
      }
    }
    SDL_DestroySurface(a); SDL_DestroySurface(b);
    SDL_DestroySurface(composed); SDL_DestroySurface(direct);
    if (i == 1) FZeroRuntimeUiOpen(rt);
    fzero_imgui_render_overlay(ig, rt, renderer, 1, 60.0);
    CHECK(SDL_GetRenderTarget(renderer) == NULL);
    CHECK(!SDL_RenderViewportSet(renderer));
    CHECK(SDL_RenderPresent(renderer));
  }
  /* Actual GPU effects must change scene RGB, preserve the HUD exactly,
   * restore Original immediately, and retain black in a completely dark frame. */
  for (int style = 1; style <= FZERO_VISUAL_HUD_DIAGNOSTIC; ++style) {
    FZeroPresentationSetStyle(video, (FZeroVisualStyle)style);
    CHECK(FZeroPresentationDraw(video));
    CHECK(FZeroPresentationMatchesMasked(video, expected, hud));
    SDL_Surface *raw = FZeroPresentationReadComposite(video); CHECK(raw);
    SDL_Surface *pixels = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888); CHECK(pixels);
    int changed = 0;
    for (int y = 0; y < 224; ++y) {
      Uint32 *row = (Uint32 *)((Uint8 *)pixels->pixels + y * pixels->pitch);
      for (int x = 0; x < 256; ++x) if (!hud[y][x]) {
        changed += (row[x] & 0xffffff) != (world[y][x] & 0xffffff);
        if ((style == FZERO_VISUAL_BLACK_AND_WHITE || style == FZERO_VISUAL_HUD_DIAGNOSTIC) &&
            FZeroPresentationHasShader(video))
          CHECK(((row[x] >> 16) & 255) == ((row[x] >> 8) & 255) &&
                ((row[x] >> 8) & 255) == (row[x] & 255));
      }
    }
    CHECK(FZeroPresentationHasShader(video) ? changed > 20000 : changed == 0);
    SDL_DestroySurface(pixels); SDL_DestroySurface(raw);
    FZeroPresentationSetStyle(video, FZERO_VISUAL_ORIGINAL);
    CHECK(FZeroPresentationDraw(video)); CHECK(FZeroPresentationMatches(video, expected));
  }
  /* Widen a synthetic scene with bright, distinct sides. Every centre pixel
   * must match the native composite, even where Enhanced samples neighbours.
   * Switch both ways without uploading another frame, as a paused menu does. */
  static Uint32 wide_world[224][FZERO_WIDE_WIDTH], wide_hud[224][FZERO_WIDE_WIDTH];
  static Uint32 native_composite[224][256];
  for (int y = 0; y < 224; ++y) {
    for (int x = 0; x < FZERO_WIDE_WIDTH; ++x)
      wide_world[y][x] = x < FZERO_WIDE_MARGIN ? 0xff0000 : 0x00ff00;
    memcpy(wide_world[y] + FZERO_WIDE_MARGIN, world[y], sizeof(world[y]));
    memcpy(wide_hud[y] + FZERO_WIDE_MARGIN, hud[y], sizeof(hud[y]));
  }
  CHECK(FZeroPresentationUploadWide(video, wide_world, wide_hud));
  for (int filter = 0; filter < 2; ++filter) for (int style = 0; style <= FZERO_VISUAL_HUD_DIAGNOSTIC; ++style) {
    CHECK(SDL_SetTextureScaleMode(texture, filter ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST));
    FZeroPresentationSetWidescreen(video, false);
    settings.widescreen = 0;
    FZeroRuntimeUiReapplyLogicalPresentation(renderer, &settings);
    FZeroPresentationSetStyle(video, (FZeroVisualStyle)style);
    CHECK(FZeroPresentationDraw(video));
    SDL_Surface *raw = FZeroPresentationReadComposite(video); CHECK(raw);
    SDL_Surface *pixels = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888); CHECK(pixels);
    for (int y = 0; y < 224; ++y)
      memcpy(native_composite[y], (Uint8 *)pixels->pixels + y * pixels->pitch, sizeof(native_composite[y]));
    SDL_DestroySurface(pixels); SDL_DestroySurface(raw);
    FZeroPresentationSetWidescreen(video, true);
    settings.widescreen = 1;
    FZeroRuntimeUiReapplyLogicalPresentation(renderer, &settings);
    CHECK(FZeroPresentationDraw(video));
    CHECK(FZeroPresentationMatches(video, native_composite));
    CHECK(FZeroPresentationMatchesMasked(video, expected, hud));
    CHECK(FZeroPresentationMatchesWide(video, wide_world, wide_hud, style != 0));
    raw = FZeroPresentationReadComposite(video); CHECK(raw);
    pixels = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888); CHECK(pixels);
    CHECK(pixels->w == FZERO_WIDE_WIDTH && pixels->h == 224);
    if (style == 0) for (int y = 0; y < 224; ++y) {
      Uint32 *row = (Uint32 *)((Uint8 *)pixels->pixels + y * pixels->pitch);
      for (int x = 0; x < FZERO_WIDE_WIDTH; ++x)
        if (x < FZERO_WIDE_MARGIN || x >= FZERO_WIDE_MARGIN + 256)
          CHECK((row[x] & 0xffffff) == wide_world[y][x]);
    }
    SDL_DestroySurface(pixels); SDL_DestroySurface(raw);
    fzero_imgui_render_overlay(ig, rt, renderer, 1, 60.0);
    int w, h; SDL_RendererLogicalPresentation mode;
    CHECK(SDL_GetRenderLogicalPresentation(renderer, &w, &h, &mode));
    CHECK(w == FZERO_WIDE_WIDTH && h == 224);
    CHECK(!SDL_GetRenderTarget(renderer) && !SDL_RenderViewportSet(renderer));
    CHECK(SDL_RenderPresent(renderer));
    FZeroPresentationSetWidescreen(video, false);
    settings.widescreen = 0;
    FZeroRuntimeUiReapplyLogicalPresentation(renderer, &settings);
    CHECK(FZeroPresentationDraw(video));
    CHECK(FZeroPresentationMatches(video, native_composite));
  }
  /* Relocated overlay pixels in both margins are included in readback checks. */
  wide_hud[10][20]=0xff123456; wide_hud[10][378]=0xffabcdef;
  CHECK(FZeroPresentationUploadWide(video,wide_world,wide_hud));
  FZeroPresentationSetWidescreen(video,true);
  for(int style=0;style<=FZERO_VISUAL_HUD_DIAGNOSTIC;++style) {
    FZeroPresentationSetStyle(video,(FZeroVisualStyle)style);
    CHECK(FZeroPresentationDraw(video));
    CHECK(FZeroPresentationMatchesWide(video,wide_world,wide_hud,style!=0));
    wide_hud[10][20]^=1;
    CHECK(!FZeroPresentationMatchesWide(video,wide_world,wide_hud,true));
    wide_hud[10][20]^=1;
  }
  /* Unclassified layouts retain Original colours across the wide image. A centre
   * comparison alone would miss Enhanced colour leaking into the margins. */
  static Uint32 fallback[224][FZERO_WIDE_WIDTH];
  for (int y = 0; y < 224; ++y) for (int x = 0; x < FZERO_WIDE_WIDTH; ++x)
    fallback[y][x] = (wide_hud[y][x] ? wide_hud[y][x] : wide_world[y][x]) | 0xff000000u;
  CHECK(FZeroPresentationUploadWide(video, wide_world, fallback));
  FZeroPresentationSetWidescreen(video, true);
  for (int style = 0; style <= FZERO_VISUAL_HUD_DIAGNOSTIC; ++style) {
    FZeroPresentationSetStyle(video, (FZeroVisualStyle)style);
    CHECK(FZeroPresentationDraw(video));
    SDL_Surface *raw = FZeroPresentationReadComposite(video); CHECK(raw);
    SDL_Surface *pixels = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888); CHECK(pixels);
    for (int y = 0; y < 224; ++y) {
      Uint32 *row = (Uint32 *)((Uint8 *)pixels->pixels + y * pixels->pitch);
      for (int x = 0; x < FZERO_WIDE_WIDTH; ++x)
        CHECK((row[x] & 0xffffff) == (fallback[y][x] & 0xffffff));
    }
    SDL_DestroySurface(pixels); SDL_DestroySurface(raw);
  }
  /* Unsupported screens use opaque black side masks. They must hide scene
   * pixels and bloom, then clear when supported capture resumes. */
  for (int y = 0; y < 224; ++y) for (int x = 0; x < FZERO_WIDE_WIDTH; ++x)
    if (x < FZERO_WIDE_MARGIN || x >= FZERO_WIDE_MARGIN + 256) wide_hud[y][x] = 0xff000000;
  CHECK(FZeroPresentationUploadWide(video, wide_world, wide_hud));
  FZeroPresentationSetWidescreen(video, true);
  FZeroPresentationSetStyle(video, FZERO_VISUAL_ENHANCED);
  CHECK(FZeroPresentationDraw(video));
  SDL_Surface *bars_raw = FZeroPresentationReadComposite(video); CHECK(bars_raw);
  SDL_Surface *bars = SDL_ConvertSurface(bars_raw, SDL_PIXELFORMAT_ARGB8888); CHECK(bars);
  for (int y = 0; y < 224; ++y) {
    Uint32 *row = (Uint32 *)((Uint8 *)bars->pixels + y * bars->pitch);
    for (int x = 0; x < FZERO_WIDE_WIDTH; ++x)
      if (x < FZERO_WIDE_MARGIN || x >= FZERO_WIDE_MARGIN + 256) CHECK(!(row[x] & 0xffffff));
  }
  SDL_DestroySurface(bars); SDL_DestroySurface(bars_raw);
  FZeroPresentationSetWidescreen(video, false);
  memset(world, 0, sizeof(world)); memset(hud, 0, sizeof(hud));
  hud[100][100] = 0xffffffff; hud[100][101] = 0xff000000;
  CHECK(FZeroPresentationUpload(video, world, hud));
  FZeroPresentationSetStyle(video, FZERO_VISUAL_ENHANCED);
  CHECK(FZeroPresentationDraw(video));
  /* A bright HUD pixel must not emit bloom into the black scene. */
  CHECK(FZeroPresentationMatches(video, hud));
  world[150][150] = 0xffffff;
  CHECK(FZeroPresentationUpload(video, world, hud));
  CHECK(FZeroPresentationDraw(video));
  CHECK(FZeroPresentationMatchesMasked(video, hud, hud));
  SDL_Surface *glow_raw = FZeroPresentationReadComposite(video); CHECK(glow_raw);
  SDL_Surface *glow = SDL_ConvertSurface(glow_raw, SDL_PIXELFORMAT_ARGB8888); CHECK(glow);
  Uint32 *glow_row = (Uint32 *)((Uint8 *)glow->pixels + 150 * glow->pitch);
  /* A bright scene pixel does emit a local glow onto its black neighbour. */
  CHECK(FZeroPresentationHasShader(video) ? (glow_row[151] & 0xffffff) != 0 :
                                           (glow_row[151] & 0xffffff) == 0);
  CHECK((glow_row[154] & 0xffffff) == 0);
  SDL_DestroySurface(glow); SDL_DestroySurface(glow_raw);
  /* Vivid increases colour separation without tinting greys or spreading
   * bright pixels into neighbours. Black, white and protected HUD stay exact. */
  memset(world,0,sizeof(world));
  world[150][150]=0x806050; world[150][151]=0x808080; world[150][152]=0xffffff;
  CHECK(FZeroPresentationUpload(video,world,hud));
  FZeroPresentationSetStyle(video,FZERO_VISUAL_VIVID);
  CHECK(FZeroPresentationDraw(video));
  CHECK(FZeroPresentationMatchesMasked(video,hud,hud));
  SDL_Surface *vivid_raw=FZeroPresentationReadComposite(video); CHECK(vivid_raw);
  SDL_Surface *vivid=SDL_ConvertSurface(vivid_raw,SDL_PIXELFORMAT_ARGB8888); CHECK(vivid);
  Uint32 *vivid_row=(Uint32*)((Uint8*)vivid->pixels+150*vivid->pitch);
  int red=(vivid_row[150]>>16)&255, blue=vivid_row[150]&255;
  CHECK(FZeroPresentationHasShader(video)?red-blue>0x30:red-blue==0x30);
  CHECK(((vivid_row[151]>>16)&255)==((vivid_row[151]>>8)&255));
  CHECK(((vivid_row[151]>>8)&255)==(vivid_row[151]&255));
  CHECK((vivid_row[152]&0xffffff)==0xffffff && !(vivid_row[153]&0xffffff));
  SDL_DestroySurface(vivid); SDL_DestroySurface(vivid_raw);
  fzero_imgui_destroy(ig); FZeroRuntimeUiDestroy(rt);
  SDL_DestroyTexture(reference);
  FZeroPresentationDestroy(video);
  SDL_DestroyWindow(window);
}
int main(void) {
#ifdef _WIN32
  /* Let automation observe a failing exit code without a modal OS dialog. */
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
  CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));
  TestSettings(); TestBindings(); TestSaveMigration(); TestFps(); TestMenu(); TestPresentation();
  SDL_Quit(); puts("host UI tests: passed"); return 0;
}
