// dear imgui: Renderer Backend for SDL_Renderer for SDL3 (LOCAL PORT)
//
// This repo pins recomp-ui, which vendors Dear ImGui v1.91.9b. That version
// ships only the SDL2 renderer backend (imgui_impl_sdlrenderer2); the upstream
// SDL3 backend arrived later and depends on ImGui APIs this pin lacks
// (ImTextureRef, ImGuiBackendFlags_RendererHasTextures). Rather than bump the
// pin or fork recomp-ui, this host carries a local port of the official
// v1.91.9b renderer2 backend to SDL3. See imgui_impl_sdlrenderer3.cpp.

// Implemented features:
//  [X] Renderer: User texture binding. Use 'SDL_Texture*' as ImTextureID. Read the FAQ about ImTextureID!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(ImGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.

#pragma once
#ifndef IMGUI_DISABLE
#include "imgui.h"      // IMGUI_IMPL_API

struct SDL_Renderer;

IMGUI_IMPL_API bool     ImGui_ImplSDLRenderer3_Init(SDL_Renderer* renderer);
IMGUI_IMPL_API void     ImGui_ImplSDLRenderer3_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplSDLRenderer3_NewFrame();
IMGUI_IMPL_API void     ImGui_ImplSDLRenderer3_RenderDrawData(ImDrawData* draw_data, SDL_Renderer* renderer);

// Called by Init/NewFrame/Shutdown
IMGUI_IMPL_API bool     ImGui_ImplSDLRenderer3_CreateFontsTexture();
IMGUI_IMPL_API void     ImGui_ImplSDLRenderer3_DestroyFontsTexture();
IMGUI_IMPL_API bool     ImGui_ImplSDLRenderer3_CreateDeviceObjects();
IMGUI_IMPL_API void     ImGui_ImplSDLRenderer3_DestroyDeviceObjects();

// Selected render state data shared with callbacks.
// This is temporarily stored in GetPlatformIO().Renderer_RenderState during the ImGui_ImplSDLRenderer3_RenderDrawData() call.
struct ImGui_ImplSDLRenderer3_RenderState
{
    SDL_Renderer*       Renderer;
};

#endif // #ifndef IMGUI_DISABLE
