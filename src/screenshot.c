#include "screenshot.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Shared with recomp-ui's launcher capture via RECOMP_UI_HOST_STB_WRITE.
 * Keep PNG support on SDL 3.2 as well as the SDL 3.4 GPU renderer. */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

static void WritePng(void *context, void *data, int size) {
    FILE *file = (FILE *)context;
    fwrite(data, 1, (size_t)size, file);
}

static SDL_Surface *ReadOutput(SDL_Renderer *renderer) {
    if (SDL_GetRenderTarget(renderer)) {
        SDL_SetError("Screenshot requires the window render target");
        return NULL;
    }
    int width, height;
    SDL_RendererLogicalPresentation mode;
    SDL_Rect viewport;
    bool viewport_set = SDL_RenderViewportSet(renderer);
    if (!SDL_GetRenderViewport(renderer, &viewport) ||
        !SDL_GetRenderLogicalPresentation(renderer, &width, &height, &mode)) return NULL;
    /* ReadPixels clips to the active viewport. Temporarily use physical output
     * coordinates to include the full window, even with integer letterboxing.
     * Already queued game drawing keeps its original scale. */
    if (!SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED)) return NULL;
    SDL_Surface *pixels = SDL_SetRenderViewport(renderer, NULL) ?
                            SDL_RenderReadPixels(renderer, NULL) : NULL;
    bool restored = SDL_SetRenderLogicalPresentation(renderer, width, height, mode);
    if (!SDL_SetRenderViewport(renderer, viewport_set ? &viewport : NULL)) restored = false;
    if (!restored) {
        SDL_DestroySurface(pixels);
        return NULL;
    }
    return pixels;
}

bool FZeroScreenshotSave(SDL_Renderer *renderer, const char *directory,
                         char *path, size_t path_size) {
    if (!path || !path_size) return SDL_SetError("Screenshot path buffer is empty");
    path[0] = '\0';
    if (!SDL_CreateDirectory(directory)) return false;
    SDL_Surface *raw = ReadOutput(renderer);
    if (!raw) return false;
    /* The legacy PPU alpha byte is unused. RGB also avoids transparent PNGs. */
    SDL_Surface *pixels = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGB24);
    SDL_DestroySurface(raw);
    if (!pixels) return false;

    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    char stamp[32];
    if (!utc || !strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%SZ", utc)) {
        SDL_DestroySurface(pixels);
        return SDL_SetError("Cannot determine screenshot time");
    }

    FILE *file = NULL;
    for (unsigned index = 0; index < 10000; ++index) {
        int length = snprintf(path, path_size, "%s/FZero_%s_%04u.png",
                              directory, stamp, index);
        if (length < 0 || (size_t)length >= path_size) {
            SDL_SetError("Screenshot path is too long");
            break;
        }
        /* Exclusive creation protects existing captures, including rapid
         * presses and another running copy of the game. */
        file = fopen(path, "wbx");
        if (file) break;
        if (errno != EEXIST) {
            SDL_SetError("Cannot create screenshot: %s", strerror(errno));
            break;
        }
        SDL_SetError("Too many screenshots for this timestamp");
    }
    if (!file) {
        path[0] = '\0';
        SDL_DestroySurface(pixels);
        return false;
    }
    int encoded = stbi_write_png_to_func(WritePng, file, pixels->w, pixels->h,
                                        3, pixels->pixels, pixels->pitch);
    bool ok = encoded && !ferror(file);
    if (fclose(file) != 0) ok = false;
    SDL_DestroySurface(pixels);
    if (!ok) {
        remove(path); /* Only the incomplete file exclusively created above. */
        path[0] = '\0';
        return SDL_SetError("Could not write screenshot PNG");
    }
    return true;
}
