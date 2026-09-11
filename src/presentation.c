#include "presentation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include "scene_dxil.h"
#endif

struct FZeroPresentation {
    SDL_Renderer *renderer;
    SDL_Texture *world, *hud, *composite;
    SDL_Texture *wide_world, *wide_hud, *wide_composite;
    bool widescreen;
    bool legacy;
    FZeroVisualStyle style;
#if SDL_VERSION_ATLEAST(3, 4, 0)
    SDL_GPUDevice *device;
    SDL_GPUShader *shader;
    SDL_GPURenderState *state;
#endif
};

#if SDL_VERSION_ATLEAST(3, 4, 0)
/* Authored here. SDL's GPU renderer supplies COLOR0 at location 0 and
 * TEXCOORD0 at location 1. Slot 0 is the SDL texture being drawn.
 * RGB is already SNES colour output; do not apply gamma conversion here.
 * The PPU's unused alpha byte is zero, so this scene pass makes it opaque. */
static const char kSceneShader[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct In { float4 color [[user(locn0)]]; float2 uv [[user(locn1)]]; };\n"
    "fragment float4 scene(In in [[stage_in]], texture2d<float> tex [[texture(0)]],\n"
    "                      sampler smp [[sampler(0)]], constant float4 &params [[buffer(0)]]) {\n"
    "    float3 c = tex.sample(smp, in.uv).rgb;\n"
    "    if (params.x < 0.5) return float4(c, 1.0);\n"
    "    float l = dot(c, float3(0.2126, 0.7152, 0.0722));\n"
    /* Black & White and the internal HUD diagnostic share the same greyscale pass. */
    "    if (params.x > 2.5) return float4(float3(l), 1.0);\n"
    /* Raise saturation around the brightest channel, preserving hue and
     * highlight strength. Bound it at full saturation, then add smooth
     * contrast. The point sample keeps pixel edges sharp. */
    "    if (params.x > 1.5) {\n"
    "        float peak = max(c.r, max(c.g, c.b));\n"
    "        float low = min(c.r, min(c.g, c.b));\n"
    "        float amount = min(1.85, peak / max(peak-low, 0.00001));\n"
    "        float3 vivid = clamp(peak + (c-peak)*amount, 0.0, 1.0);\n"
    "        vivid += 0.22 * (vivid-0.5) * 4.0 * vivid * (1.0-vivid);\n"
    "        return float4(clamp(vivid, 0.0, 1.0), 1.0);\n"
    "    }\n"
    "    float3 graded = mix(float3(l), c, 1.10);\n"
    "    graded += (graded - 0.5) * 0.055 * min(l * 4.0, 1.0);\n"
    "    graded += float3(-0.010, 0.003, 0.018) * l * (1.0 - l);\n"
    "    float2 lo = params.yz * 0.5, hi = 1.0 - lo;\n"
    "    if (params.w > 0.0 && in.uv.x >= params.w * params.y &&\n"
    "        in.uv.x < (params.w + 256.0) * params.y) {\n"
    "        lo.x = (params.w + 0.5) * params.y;\n"
    "        hi.x = (params.w + 255.5) * params.y;\n"
    "    }\n"
    "    float3 glow = float3(0.0);\n"
    "    for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x) {\n"
    "        float2 uv = clamp(in.uv + float2(x, y) * params.yz,\n"
    "                          lo, hi);\n"
    "        float3 tap = tex.sample(smp, uv).rgb;\n"
    "        float peak = max(tap.r, max(tap.g, tap.b));\n"
    "        float weight = float((3 - abs(x)) * (3 - abs(y))) / 81.0;\n"
    "        glow += tap * smoothstep(0.65, 1.0, peak) * weight;\n"
    "    }\n"
    "    return float4(clamp(graded + glow * 0.18, 0.0, 1.0), 1.0);\n"
    "}\n";

static bool CreateShader(FZeroPresentation *v) {
    v->device = SDL_GetPointerProperty(SDL_GetRendererProperties(v->renderer),
                                       SDL_PROP_RENDERER_GPU_DEVICE_POINTER, NULL);
    if (!v->device) return false;
    SDL_GPUShaderCreateInfo info = {0};
#ifdef _WIN32
    info.code = kSceneShaderDXIL;
    info.code_size = sizeof(kSceneShaderDXIL);
    info.format = SDL_GPU_SHADERFORMAT_DXIL;
#else
    info.code = (const Uint8 *)kSceneShader;
    info.code_size = sizeof(kSceneShader) - 1;
    info.format = SDL_GPU_SHADERFORMAT_MSL;
#endif
    if (!(SDL_GetGPUShaderFormats(v->device) & info.format)) return false;
    info.entrypoint = "scene";
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = 1;
    info.num_uniform_buffers = 1;
    v->shader = SDL_CreateGPUShader(v->device, &info);
    if (!v->shader) return false;
    SDL_GPURenderStateCreateInfo state = {0};
    state.fragment_shader = v->shader;
    v->state = SDL_CreateGPURenderState(v->renderer, &state);
    return v->state != NULL;
}

static void DestroyShader(FZeroPresentation *v) {
    if (v->state) SDL_DestroyGPURenderState(v->state);
    if (v->shader) SDL_ReleaseGPUShader(v->device, v->shader);
    v->state = NULL;
    v->shader = NULL;
    v->device = NULL;
}
#endif

FZeroPresentation *FZeroPresentationCreate(SDL_Window *window, bool legacy) {
    FZeroPresentation *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->legacy = legacy;
#if (defined(__APPLE__) || defined(_WIN32)) && SDL_VERSION_ATLEAST(3, 4, 0)
    if (!legacy && strcmp(SDL_GetCurrentVideoDriver(), "dummy")) {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
        SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, "gpu");
#ifdef _WIN32
        SDL_SetBooleanProperty(props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_DXIL_BOOLEAN, true);
#else
        SDL_SetBooleanProperty(props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_MSL_BOOLEAN, true);
#endif
        v->renderer = SDL_CreateRendererWithProperties(props);
        SDL_DestroyProperties(props);
        if (v->renderer && !CreateShader(v)) {
            fprintf(stderr, "[Video] Shader setup failed: %s; using SDL composition\n", SDL_GetError());
            DestroyShader(v);
            SDL_DestroyRenderer(v->renderer);
            v->renderer = NULL;
        }
    }
#endif
    if (!v->renderer) v->renderer = SDL_CreateRenderer(window, NULL);
    if (!v->renderer) goto fail;
    v->world = SDL_CreateTexture(v->renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, FZERO_VIDEO_WIDTH, FZERO_VIDEO_HEIGHT);
    if (!v->world || !SDL_SetTextureBlendMode(v->world, SDL_BLENDMODE_NONE)) goto fail;
    if (!legacy) {
        v->hud = SDL_CreateTexture(v->renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, FZERO_VIDEO_WIDTH, FZERO_VIDEO_HEIGHT);
        v->composite = SDL_CreateTexture(v->renderer, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_TARGET, FZERO_VIDEO_WIDTH, FZERO_VIDEO_HEIGHT);
        if (!v->hud || !v->composite ||
            !SDL_SetTextureBlendMode(v->hud, SDL_BLENDMODE_BLEND) ||
            !SDL_SetTextureScaleMode(v->hud, SDL_SCALEMODE_NEAREST) ||
            !SDL_SetTextureBlendMode(v->composite, SDL_BLENDMODE_NONE)) goto fail;
    }
    if (!legacy) {
        v->wide_world = SDL_CreateTexture(v->renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, FZERO_WIDE_WIDTH, FZERO_VIDEO_HEIGHT);
        v->wide_hud = SDL_CreateTexture(v->renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, FZERO_WIDE_WIDTH, FZERO_VIDEO_HEIGHT);
        v->wide_composite = SDL_CreateTexture(v->renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_TARGET, FZERO_WIDE_WIDTH, FZERO_VIDEO_HEIGHT);
        if (!v->wide_world || !v->wide_hud || !v->wide_composite ||
            !SDL_SetTextureBlendMode(v->wide_world, SDL_BLENDMODE_NONE) ||
            !SDL_SetTextureBlendMode(v->wide_hud, SDL_BLENDMODE_BLEND) ||
            !SDL_SetTextureScaleMode(v->wide_hud, SDL_SCALEMODE_NEAREST) ||
            !SDL_SetTextureBlendMode(v->wide_composite, SDL_BLENDMODE_NONE)) goto fail;
    }
    fprintf(stderr, "[Video] %s, renderer=%s\n",
            FZeroPresentationHasShader(v) ? "GPU scene shader + HUD composition" :
            legacy ? "legacy single texture" : "SDL scene + HUD composition (no custom shader)",
            SDL_GetRendererName(v->renderer));
#if SDL_VERSION_ATLEAST(3, 4, 0)
    if (v->device) fprintf(stderr, "[Video] GPU backend: %s\n", SDL_GetGPUDeviceDriver(v->device));
#endif
    return v;
fail:
    FZeroPresentationDestroy(v);
    return NULL;
}

void FZeroPresentationDestroy(FZeroPresentation *v) {
    if (!v) return;
#if SDL_VERSION_ATLEAST(3, 4, 0)
    DestroyShader(v);
#endif
    SDL_DestroyTexture(v->wide_composite);
    SDL_DestroyTexture(v->wide_hud);
    SDL_DestroyTexture(v->wide_world);
    SDL_DestroyTexture(v->composite);
    SDL_DestroyTexture(v->hud);
    SDL_DestroyTexture(v->world);
    SDL_DestroyRenderer(v->renderer);
    free(v);
}

SDL_Renderer *FZeroPresentationRenderer(FZeroPresentation *v) { return v->renderer; }
SDL_Texture *FZeroPresentationTexture(FZeroPresentation *v) { return v->world; }
bool FZeroPresentationHasShader(const FZeroPresentation *v) {
#if SDL_VERSION_ATLEAST(3, 4, 0)
    return v->state != NULL;
#else
    return false;
#endif
}

void FZeroPresentationSetStyle(FZeroPresentation *v, FZeroVisualStyle style) {
    v->style = FZeroPresentationHasShader(v) && style >= FZERO_VISUAL_ORIGINAL &&
               style <= FZERO_VISUAL_HUD_DIAGNOSTIC ? style : FZERO_VISUAL_ORIGINAL;
}

bool FZeroPresentationUpload(FZeroPresentation *v, const void *world, const void *hud) {
    return SDL_UpdateTexture(v->world, NULL, world, FZERO_VIDEO_WIDTH * 4) &&
           (v->legacy || SDL_UpdateTexture(v->hud, NULL, hud, FZERO_VIDEO_WIDTH * 4));
}

bool FZeroPresentationUploadWide(FZeroPresentation *v, const void *world, const void *hud) {
    return v->legacy || (SDL_UpdateTexture(v->wide_world, NULL, world, FZERO_WIDE_WIDTH * 4) &&
                        SDL_UpdateTexture(v->wide_hud, NULL, hud, FZERO_WIDE_WIDTH * 4));
}

void FZeroPresentationSetWidescreen(FZeroPresentation *v, bool widescreen) {
    v->widescreen = widescreen;
}

bool FZeroPresentationDraw(FZeroPresentation *v) {
    SDL_Renderer *r = v->renderer;
    if (v->legacy) {
        SDL_FRect centre = {v->widescreen ? FZERO_WIDE_MARGIN : 0, 0,
                            FZERO_VIDEO_WIDTH, FZERO_VIDEO_HEIGHT};
        return SDL_RenderTexture(r, v->world, NULL, &centre);
    }
    SDL_Texture *world = v->widescreen ? v->wide_world : v->world;
    SDL_Texture *hud = v->widescreen ? v->wide_hud : v->hud;
    SDL_Texture *composite = v->widescreen ? v->wide_composite : v->composite;
    int width = FZeroDisplayWidth(v->widescreen);
    SDL_ScaleMode scale;
    if (!SDL_GetTextureScaleMode(v->world, &scale) ||
        !SDL_SetTextureScaleMode(world, scale) || !SDL_SetTextureScaleMode(composite, scale)) return false;
    SDL_Texture *previous = SDL_GetRenderTarget(r);
    if (!SDL_SetRenderTarget(r, composite)) return false;
    bool ok = true;
#if SDL_VERSION_ATLEAST(3, 4, 0)
    if (v->state) {
        const float params[4] = {(float)v->style, 1.0f / width,
                                 1.0f / FZERO_VIDEO_HEIGHT, v->widescreen ? FZERO_WIDE_MARGIN : 0.0f};
        ok = SDL_SetGPURenderStateFragmentUniforms(v->state, 0, params, sizeof(params)) &&
             SDL_SetGPURenderState(r, v->state);
    }
#endif
    if (ok) ok = SDL_RenderTexture(r, world, NULL, NULL);
#if SDL_VERSION_ATLEAST(3, 4, 0)
    if (v->state && !SDL_SetGPURenderState(r, NULL)) ok = false;
#endif
    if (ok) ok = SDL_RenderTexture(r, hud, NULL, NULL);
    if (!SDL_SetRenderTarget(r, previous)) ok = false;
    return ok && SDL_RenderTexture(r, composite, NULL, NULL);
}

SDL_Surface *FZeroPresentationReadComposite(FZeroPresentation *v) {
    if (!v->composite) return NULL;
    SDL_Texture *previous = SDL_GetRenderTarget(v->renderer);
    if (!SDL_SetRenderTarget(v->renderer, v->widescreen ? v->wide_composite : v->composite)) return NULL;
    SDL_Surface *pixels = SDL_RenderReadPixels(v->renderer, NULL);
    if (!SDL_SetRenderTarget(v->renderer, previous)) {
        SDL_DestroySurface(pixels);
        return NULL;
    }
    return pixels;
}

bool FZeroPresentationMatchesMasked(FZeroPresentation *v, const void *reference,
                                    const void *hud) {
    SDL_Surface *raw = FZeroPresentationReadComposite(v);
    if (!raw) return false;
    SDL_Surface *pixels = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(raw);
    if (!pixels) return false;
    bool ok = pixels->w == FZeroDisplayWidth(v->widescreen) && pixels->h == FZERO_VIDEO_HEIGHT;
    const Uint32 *expected = reference, *mask = hud;
    for (int y = 0; ok && y < FZERO_VIDEO_HEIGHT; ++y) {
        const Uint32 *row = (const Uint32 *)((const Uint8 *)pixels->pixels + y * pixels->pitch) +
                            (v->widescreen ? FZERO_WIDE_MARGIN : 0);
        for (int x = 0; x < FZERO_VIDEO_WIDTH; ++x) {
            if (mask && !(mask[y * FZERO_VIDEO_WIDTH + x] >> 24)) continue;
            if ((row[x] & 0xffffff) != (expected[y * FZERO_VIDEO_WIDTH + x] & 0xffffff)) {
                SDL_SetError("Presentation RGB differs at (%d,%d): %06x != %06x", x, y,
                              row[x] & 0xffffff, expected[y * FZERO_VIDEO_WIDTH + x] & 0xffffff);
                ok = false;
                break;
            }
        }
    }
    SDL_DestroySurface(pixels);
    return ok;
}

bool FZeroPresentationMatches(FZeroPresentation *v, const void *reference) {
    return FZeroPresentationMatchesMasked(v, reference, NULL);
}

bool FZeroPresentationMatchesWide(FZeroPresentation *v, const void *world,
                                 const void *hud, bool hud_only) {
    SDL_Surface *raw=FZeroPresentationReadComposite(v);
    if(!raw) return false;
    SDL_Surface *pixels=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(raw);
    if(!pixels) return false;
    bool ok=pixels->w==FZERO_WIDE_WIDTH && pixels->h==FZERO_VIDEO_HEIGHT;
    const Uint32 *scene=world, *overlay=hud;
    for(int y=0;ok && y<FZERO_VIDEO_HEIGHT;++y) {
        const Uint32 *row=(const Uint32*)((const Uint8*)pixels->pixels+y*pixels->pitch);
        for(int x=0;x<FZERO_WIDE_WIDTH;++x) {
            int i=y*FZERO_WIDE_WIDTH+x;
            bool protected=(overlay[i]>>24)!=0;
            if(hud_only && !protected) continue;
            Uint32 expected=protected?overlay[i]:scene[i];
            if((row[x]&0xffffff)!=(expected&0xffffff)) {
                SDL_SetError("Wide presentation RGB differs at (%d,%d): %06x != %06x",
                             x,y,row[x]&0xffffff,expected&0xffffff);
                ok=false;
                break;
            }
        }
    }
    SDL_DestroySurface(pixels);
    return ok;
}
