#pragma once

/* Dear ImGui presentation glue for the in-game runtime settings overlay.
 * The host is C; Dear ImGui is C++, so the ImGui context/backends/frame live
 * in this one C++ translation unit (runtime_ui_imgui.cpp) behind a C ABI.
 *
 * Owns its own ImGui context: recomp-ui's launcher creates and destroys its
 * context inside recomp_launcher_run_window(), so by the time the game loop
 * starts ImGui is fully torn down and the host is free to create another.
 * Uses the shared SDL3 platform backend (already compiled by the launcher
 * helper) plus the locally ported SDL3 renderer backend
 * (imgui_impl_sdlrenderer3.cpp).
 *
 * SDL3-only, like the rest of the desktop host's render path; the SDL2 build
 * keeps the pre-overlay host (see CMakeLists.txt). */

#include "runtime_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FZeroImGui FZeroImGui;

/* Creates the context, loads the staged Lato font (same asset/ranges as the
 * pre-boot launcher), and initializes both backends for the game window. */
FZeroImGui *fzero_imgui_create(SDL_Window *window, SDL_Renderer *renderer);
void fzero_imgui_destroy(FZeroImGui *ig);

/* Brief status message, rendered outside the captured game composite. */
void fzero_imgui_notify(FZeroImGui *ig, const char *message);
int fzero_imgui_has_notification(FZeroImGui *ig);

/* Forward mouse/window events to the ImGui platform backend (only meaningful
 * while the overlay is open, so the host gates calls on that). */
void fzero_imgui_process_event(FZeroImGui *ig, const SDL_Event *event);

/* Render the overlay into the active renderer. Temporarily disables the
 * 256x224 logical presentation so ImGui draws at the true output size (the
 * SDL_Renderer backend's documented requirement), draws the open menu and
 * optional FPS readout, then
 * restores the renderer to the game's expected state (full-target viewport +
 * logical presentation re-enabled) before returning; the host presents
 * afterwards. Pass a negative fps while the first sample is pending. */
void fzero_imgui_render_overlay(FZeroImGui *ig, FZeroRuntimeUi *rt,
                             SDL_Renderer *renderer, int show_fps, double fps);

#ifdef __cplusplus
}
#endif
