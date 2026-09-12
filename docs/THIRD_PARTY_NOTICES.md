# Third-party notices

The SDL3 renderer backend in `src/imgui_impl_sdlrenderer3.cpp` and its header
reuse the Super Tennis port of Dear ImGui v1.91.9b's SDL2 renderer backend.
The MIT licence below covers that backend. Other dependencies retain their
own terms: [snesrecomp](../snesrecomp/LICENSE), its
[third-party notices](../snesrecomp/THIRD_PARTY_ATTRIBUTION.md),
[recomp-ui](../recomp-ui/LICENSE), and
[Dear ImGui](../recomp-ui/src/third_party/imgui/LICENSE.txt).

The build-local launcher backend modified by `patches/recomp-ui/` retains
[recomp-ui's MIT licence](../recomp-ui/LICENSE).

Release packages also include SDL3, tinyfiledialogs, stb_image, and their
licence texts. Bundled launcher fonts retain their separate terms in
[font notices](../assets/licenses/fonts.txt).

Windows release packages also include the required x64 Microsoft Visual C++
runtime DLLs from the installed Visual Studio redistributable directory, with
Microsoft's redistribution notice. The HLSL scene shader is authored in this
project. DXC is a build-time tool and is not included in the game ZIP.

The MIT License (MIT)

Copyright (c) 2014-2025 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
