# Compile once and embed in both the game and its presentation tests.
# Windows SDK's DXC emits validated DXIL; no runtime compiler DLL is needed.
if(WIN32)
    find_package(SDL3 3.4 CONFIG REQUIRED)
    find_program(FZERO_DXC NAMES dxc
        HINTS "$ENV{WindowsSdkVerBinPath}/x64" "$ENV{WindowsSdkBinPath}/$ENV{WindowsSDKVersion}/x64"
        DOC "Windows SDK DirectX Shader Compiler (x64)")
    if(NOT FZERO_DXC)
        message(FATAL_ERROR "DXC is required for Windows visual filters. Install a recent Windows SDK and use build.ps1, or set FZERO_DXC to dxc.exe.")
    endif()
    set(FZERO_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${FZERO_SHADER_DIR}")
    set(FZERO_SHADER_HEADER "${FZERO_SHADER_DIR}/scene_dxil.h")
    add_custom_command(OUTPUT "${FZERO_SHADER_HEADER}"
        BYPRODUCTS "${FZERO_SHADER_DIR}/scene.dxil"
        COMMAND "${FZERO_DXC}" -T ps_6_0 -E scene -Ges -O3
            -Fh "${FZERO_SHADER_HEADER}" -Vn kSceneShaderDXIL
            -Fo "${FZERO_SHADER_DIR}/scene.dxil"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/scene.hlsl"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/scene.hlsl"
        COMMENT "Compiling F-Zero visual filters to DXIL"
        VERBATIM)
    add_custom_target(fzero_shaders DEPENDS "${FZERO_SHADER_HEADER}")
endif()

function(fzero_target_shaders target)
    if(WIN32)
        add_dependencies(${target} fzero_shaders)
        target_include_directories(${target} PRIVATE "${FZERO_SHADER_DIR}")
    endif()
endfunction()
