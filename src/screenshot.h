#pragma once

#include <SDL3/SDL.h>

/* Save the window's completed game image, after scaling and before UI, as RGB
 * PNG at the renderer's output resolution, including letterboxing. Call after
 * drawing the game and before drawing overlays or presenting the frame.
 * Creates directory if needed. On success path contains the new filename;
 * failures leave it empty and report details through SDL_GetError(). */
bool FZeroScreenshotSave(SDL_Renderer *renderer, const char *directory,
                         char *path, size_t path_size);
