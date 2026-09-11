// dear imgui: Renderer Backend for SDL_Renderer for SDL3 (LOCAL PORT)
//
// MIT-licensed port of Dear ImGui v1.91.9b's official
// imgui_impl_sdlrenderer2.cpp (the version vendored by our pinned recomp-ui),
// adapted to the SDL3 renderer API:
//   - SDL_RenderSetViewport/GetViewport        -> SDL_SetRenderViewport/SDL_GetRenderViewport
//   - SDL_RenderSetClipRect/GetClipRect        -> SDL_SetRenderClipRect/SDL_GetRenderClipRect
//   - SDL_RenderIsClipEnabled                  -> SDL_RenderClipEnabled
//   - SDL_RenderGetScale                       -> SDL_GetRenderScale
//   - SDL_RenderGeometryRaw() vertex colors    -> SDL_FColor (floats), so the
//     packed RGBA8 ImGui vertex colors are converted to a scratch SDL_FColor
//     array per draw list.
// The clip/scale math and render-state backup/restore are unchanged from the
// official backend. This file is kept in-tree so the recomp-ui pin stays
// untouched; the upstream imgui_impl_sdlrenderer3 requires newer ImGui APIs
// (ImTextureRef, ImGuiBackendFlags_RendererHasTextures). The desktop host's
// render path is SDL3-only, so this backend is SDL3-only by construction.

// Implemented features:
//  [X] Renderer: User texture binding. Use 'SDL_Texture*' as ImTextureID. Read the FAQ about ImTextureID!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(ImGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.

// You can copy and use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_sdlrenderer3.h"
#include <stdio.h>      // fprintf
#include <stdint.h>     // intptr_t

// Clang warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"    // warning: implicit conversion changes signedness
#endif

// SDL3 (the desktop host render path is SDL3-only; see sdl_compat.h)
#include "desktop/sdl_compat.h"

// SDL_Renderer data
struct ImGui_ImplSDLRenderer3_Data
{
    SDL_Renderer*   Renderer;       // Main viewport's renderer
    SDL_Texture*    FontTexture;
    ImGui_ImplSDLRenderer3_Data()   { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
static ImGui_ImplSDLRenderer3_Data* ImGui_ImplSDLRenderer3_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplSDLRenderer3_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

// Functions
bool ImGui_ImplSDLRenderer3_Init(SDL_Renderer* renderer)
{
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");
    IM_ASSERT(renderer != nullptr && "SDL_Renderer not initialized!");

    // Setup backend capabilities flags
    ImGui_ImplSDLRenderer3_Data* bd = IM_NEW(ImGui_ImplSDLRenderer3_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_sdlrenderer3";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.

    bd->Renderer = renderer;

    return true;
}

void ImGui_ImplSDLRenderer3_Shutdown()
{
    ImGui_ImplSDLRenderer3_Data* bd = ImGui_ImplSDLRenderer3_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplSDLRenderer3_DestroyDeviceObjects();

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    IM_DELETE(bd);
}

static void ImGui_ImplSDLRenderer3_SetupRenderState(SDL_Renderer* renderer)
{
    // Clear out any viewports and cliprect set by the user
    // FIXME: Technically speaking there are lots of other things we could backup/setup/restore during our render process.
    SDL_SetRenderViewport(renderer, nullptr);
    SDL_SetRenderClipRect(renderer, nullptr);
}

void ImGui_ImplSDLRenderer3_NewFrame()
{
    ImGui_ImplSDLRenderer3_Data* bd = ImGui_ImplSDLRenderer3_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplSDLRenderer3_Init()?");

    if (!bd->FontTexture)
        ImGui_ImplSDLRenderer3_CreateDeviceObjects();
}

void ImGui_ImplSDLRenderer3_RenderDrawData(ImDrawData* draw_data, SDL_Renderer* renderer)
{
    // If there's a scale factor set by the user, use that instead
    float rsx = 1.0f;
    float rsy = 1.0f;
    SDL_GetRenderScale(renderer, &rsx, &rsy);
    ImVec2 render_scale;
    render_scale.x = (rsx == 1.0f) ? draw_data->FramebufferScale.x : 1.0f;
    render_scale.y = (rsy == 1.0f) ? draw_data->FramebufferScale.y : 1.0f;

    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    int fb_width = (int)(draw_data->DisplaySize.x * render_scale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * render_scale.y);
    if (fb_width == 0 || fb_height == 0)
        return;

    // Backup SDL_Renderer state that will be modified to restore it afterwards
    struct BackupSDLRendererState
    {
        SDL_Rect    Viewport;
        bool        ClipEnabled;
        SDL_Rect    ClipRect;
    };
    BackupSDLRendererState old = {};
    old.ClipEnabled = SDL_RenderClipEnabled(renderer);
    SDL_GetRenderViewport(renderer, &old.Viewport);
    SDL_GetRenderClipRect(renderer, &old.ClipRect);

    // Setup desired state
    ImGui_ImplSDLRenderer3_SetupRenderState(renderer);

    // Setup render state structure (for callbacks and custom texture bindings)
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ImGui_ImplSDLRenderer3_RenderState render_state;
    render_state.Renderer = renderer;
    platform_io.Renderer_RenderState = &render_state;

    // Will project scissor/clipping rectangles into framebuffer space
    ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
    ImVec2 clip_scale = render_scale;

    // Render command lists
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* draw_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx_buffer = draw_list->VtxBuffer.Data;
        const ImDrawIdx* idx_buffer = draw_list->IdxBuffer.Data;

        // SDL3's SDL_RenderGeometryRaw() takes SDL_FColor (floats 0..1) while
        // ImGui packs RGBA8 into ImDrawVert::col; convert the whole list once
        // so per-command color pointers can just offset into the array.
        ImVector<SDL_FColor> colors;
        colors.resize(draw_list->VtxBuffer.Size);
        for (int i = 0; i < draw_list->VtxBuffer.Size; i++)
        {
            const ImU32 c = vtx_buffer[i].col;
            colors[i].r = (float)((c >>  0) & 0xFF) / 255.0f;
            colors[i].g = (float)((c >>  8) & 0xFF) / 255.0f;
            colors[i].b = (float)((c >> 16) & 0xFF) / 255.0f;
            colors[i].a = (float)((c >> 24) & 0xFF) / 255.0f;
        }

        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplSDLRenderer3_SetupRenderState(renderer);
                else
                    pcmd->UserCallback(draw_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
                if (clip_min.x < 0.0f) { clip_min.x = 0.0f; }
                if (clip_min.y < 0.0f) { clip_min.y = 0.0f; }
                if (clip_max.x > (float)fb_width) { clip_max.x = (float)fb_width; }
                if (clip_max.y > (float)fb_height) { clip_max.y = (float)fb_height; }
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                SDL_Rect r = { (int)(clip_min.x), (int)(clip_min.y), (int)(clip_max.x - clip_min.x), (int)(clip_max.y - clip_min.y) };
                SDL_SetRenderClipRect(renderer, &r);

                const float* xy = (const float*)(const void*)((const char*)(vtx_buffer + pcmd->VtxOffset) + offsetof(ImDrawVert, pos));
                const float* uv = (const float*)(const void*)((const char*)(vtx_buffer + pcmd->VtxOffset) + offsetof(ImDrawVert, uv));
                const SDL_FColor* color = colors.Data + pcmd->VtxOffset;

                if (!SDL_RenderGeometryRaw(renderer,
                        (SDL_Texture*)pcmd->TextureId,
                        xy, sizeof(ImDrawVert),
                        color, sizeof(SDL_FColor),
                        uv, sizeof(ImDrawVert),
                        pcmd->ElemCount,
                        idx_buffer + pcmd->IdxOffset, pcmd->ElemCount,
                        (int)sizeof(ImDrawIdx))) {
                    // The official backend ignores this; a failed geometry
                    // call would otherwise silently blank the overlay.
                    static bool warned;
                    if (!warned) {
                        warned = true;
                        fprintf(stderr,
                            "imgui_impl_sdlrenderer3: SDL_RenderGeometryRaw "
                            "failed: %s\n", SDL_GetError());
                    }
                }
            }
        }
    }

    // Restore modified SDL_Renderer state
    SDL_SetRenderViewport(renderer, &old.Viewport);
    SDL_SetRenderClipRect(renderer, old.ClipEnabled ? &old.ClipRect : nullptr);
}

// ImTextureID as SDL_Texture*
bool ImGui_ImplSDLRenderer3_CreateFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplSDLRenderer3_Data* bd = ImGui_ImplSDLRenderer3_GetBackendData();

    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bit (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save GPU memory.

    // Upload texture to graphics system
    SDL_Texture* texture = SDL_CreateTexture(bd->Renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
    if (texture == nullptr)
        return false;
    SDL_UpdateTexture(texture, nullptr, pixels, width * 4);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    // Store our identifier
    io.Fonts->SetTexID((ImTextureID)(intptr_t)texture);
    bd->FontTexture = texture;

    return true;
}

void ImGui_ImplSDLRenderer3_DestroyFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplSDLRenderer3_Data* bd = ImGui_ImplSDLRenderer3_GetBackendData();
    if (bd->FontTexture)
    {
        SDL_DestroyTexture(bd->FontTexture);
        bd->FontTexture = nullptr;
        io.Fonts->SetTexID((ImTextureID)(intptr_t)0);
    }
}

bool ImGui_ImplSDLRenderer3_CreateDeviceObjects()
{
    return ImGui_ImplSDLRenderer3_CreateFontsTexture();
}

void ImGui_ImplSDLRenderer3_DestroyDeviceObjects()
{
    ImGui_ImplSDLRenderer3_DestroyFontsTexture();
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef IMGUI_DISABLE
