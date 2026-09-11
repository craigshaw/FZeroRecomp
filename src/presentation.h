#pragma once
#include <SDL3/SDL.h>
#include "display_layout.h"

enum { FZERO_VIDEO_WIDTH = FZERO_NATIVE_WIDTH, FZERO_VIDEO_HEIGHT = FZERO_DISPLAY_HEIGHT };
typedef struct FZeroPresentation FZeroPresentation;
typedef enum FZeroVisualStyle {
    FZERO_VISUAL_ORIGINAL, FZERO_VISUAL_ENHANCED, FZERO_VISUAL_VIVID,
    FZERO_VISUAL_BLACK_AND_WHITE, FZERO_VISUAL_HUD_DIAGNOSTIC
} FZeroVisualStyle;

/* Prefer custom Metal (Mac) or DXIL (Windows) shaders; use SDL composition if unavailable.
 * legacy requests the original single-texture path for comparison. */
FZeroPresentation *FZeroPresentationCreate(SDL_Window *window, bool legacy);
void FZeroPresentationDestroy(FZeroPresentation *video);
SDL_Renderer *FZeroPresentationRenderer(FZeroPresentation *video);
SDL_Texture *FZeroPresentationTexture(FZeroPresentation *video);
bool FZeroPresentationHasShader(const FZeroPresentation *video);
/* Changes the next draw, including while paused. Unsupported renderers use Original. */
void FZeroPresentationSetStyle(FZeroPresentation *video, FZeroVisualStyle style);
bool FZeroPresentationUpload(FZeroPresentation *video, const void *world,
                             const void *hud);
/* Wide surfaces are 398x224. The original surfaces stay resident so view
 * changes while paused do not require another game frame. */
bool FZeroPresentationUploadWide(FZeroPresentation *video, const void *world, const void *hud);
void FZeroPresentationSetWidescreen(FZeroPresentation *video, bool widescreen);
/* Compose at native resolution before scaling, preserving filtering at HUD
 * edges. The menu and FPS pass follow this call with default shader state. */
bool FZeroPresentationDraw(FZeroPresentation *video);
/* Read the native composite for an explicit diagnostic, never normal play. */
SDL_Surface *FZeroPresentationReadComposite(FZeroPresentation *video);
/* Slow diagnostic: compare GPU RGB output with the original PPU image. */
bool FZeroPresentationMatches(FZeroPresentation *video, const void *reference);

/* Compare the original 256-column centre, in either view. With a HUD mask,
 * compare only protected pixels. NULL compares every central pixel. */
bool FZeroPresentationMatchesMasked(FZeroPresentation *video, const void *reference,
                                    const void *hud);
/* Compare the complete wide CPU composition, or only its protected HUD.
 * Relocated HUD pixels intentionally no longer match the native centre. */
bool FZeroPresentationMatchesWide(FZeroPresentation *video, const void *world,
                                 const void *hud, bool hud_only);
