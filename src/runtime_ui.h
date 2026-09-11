#pragma once

/* In-game runtime settings overlay (recomp-ui RecompRuntimeUi) adapter for the
 * desktop host. Only compiled into the launcher-enabled desktop target; see
 * CMakeLists.txt. Host responsibilities per recomp-ui docs/RUNTIME_UI.md:
 * menu input routing, pause/resume policy, live application + persistence of
 * values through callbacks, and render-frame submission (see
 * runtime_ui_imgui.cpp). */

#include "config.h"
#include "recomp_runtime_ui.h"
#include "desktop/sdl_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FZeroRuntimeUi FZeroRuntimeUi;

/* Creates the menu model over the host's live settings; window/renderer/
 * texture/audio_stream are used by the live-apply callbacks. audio_stream may
 * be NULL (audio disabled); the menu pauses/resumes the device while open. */
FZeroRuntimeUi *FZeroRuntimeUiCreate(FZeroSettings *settings,
                                                 SDL_Window *window,
                                                 SDL_Renderer *renderer,
                                                 SDL_Texture *texture,
                                                 SDL_AudioStream *audio_stream);
/* Disable Enhanced when the active renderer cannot apply its shader. */
void FZeroRuntimeUiSetEnhancedAvailable(FZeroRuntimeUi *rt, int available);
void FZeroRuntimeUiDestroy(FZeroRuntimeUi *rt);

int FZeroRuntimeUiIsOpen(const FZeroRuntimeUi *rt);
void FZeroRuntimeUiOpen(FZeroRuntimeUi *rt);

/* Route a KEY_DOWN/UP or gamepad BUTTON/AXIS event to the menu. Returns 1
 * when the event was consumed; 0 passes it through to the game. Keyboard: F1
 * toggles the menu; every other key reaches the game when the menu is closed
 * and is withheld while it is open. Gamepad: Select+Start together opens the
 * menu (Start alone stays the game's button); while open A=accept, B=back,
 * Start=close, D-pad navigates. */
int FZeroRuntimeUiHandleEvent(FZeroRuntimeUi *rt,
                                    const SDL_Event *event);

/* Re-apply the renderer's selected logical presentation from settings (used by
 * the stretch live-apply). */
void FZeroRuntimeUiReapplyLogicalPresentation(
    SDL_Renderer *renderer, const FZeroSettings *settings);

/* Hand the renderer back to the game after the overlay pass: full-target
 * viewport plus the settings' logical presentation. Called by the ImGui glue
 * before the host presents (see runtime_ui_imgui.cpp). */
void FZeroRuntimeUiRestoreRendererState(FZeroRuntimeUi *rt,
                                              SDL_Renderer *renderer);

/* The underlying recomp-ui menu (for the ImGui presentation backend). */
RecompRuntimeUi *FZeroRuntimeUiCore(const FZeroRuntimeUi *rt);

/* Drop chord state on focus loss, input-source change, or disconnection. */
void FZeroRuntimeUiResetPad(FZeroRuntimeUi *rt);

#ifdef __cplusplus
}
#endif
