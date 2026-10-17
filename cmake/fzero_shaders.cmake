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

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(FZERO_LINUX_VULKAN "Build Vulkan visual filters for Linux" ON)
    if(FZERO_LINUX_VULKAN)
        find_package(SDL3 3.4 CONFIG REQUIRED)
        find_program(FZERO_DXC NAMES dxc DOC "DirectX Shader Compiler with SPIR-V support")
        if(NOT FZERO_DXC)
            message(FATAL_ERROR "DXC is required for Linux visual filters. Run python3 build-linux.py with your ROM, set FZERO_DXC to the Linux dxc executable, or set FZERO_LINUX_VULKAN=OFF for Original-colour SDL composition. See the Linux build instructions in README.md.")
        endif()
        set(FZERO_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
        file(MAKE_DIRECTORY "${FZERO_SHADER_DIR}")
        set(FZERO_SHADER_HEADER "${FZERO_SHADER_DIR}/scene_spirv.h")
        add_custom_command(OUTPUT "${FZERO_SHADER_HEADER}"
            BYPRODUCTS "${FZERO_SHADER_DIR}/scene.spv"
            COMMAND "${FZERO_DXC}" -spirv -T ps_6_0 -E scene -Ges -O3
                -fspv-target-env=vulkan1.0 -D FZERO_SPIRV=1
                -Fh "${FZERO_SHADER_HEADER}" -Vn kSceneShaderSPIRV
                -Fo "${FZERO_SHADER_DIR}/scene.spv"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/scene.hlsl"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/scene.hlsl"
            COMMENT "Compiling F-Zero visual filters to SPIR-V"
            VERBATIM)
        add_custom_target(fzero_shaders DEPENDS "${FZERO_SHADER_HEADER}")
    endif()
endif()

function(fzero_target_shaders target)
    if(WIN32)
        add_dependencies(${target} fzero_shaders)
        target_include_directories(${target} PRIVATE "${FZERO_SHADER_DIR}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND FZERO_LINUX_VULKAN)
        add_dependencies(${target} fzero_shaders)
        target_include_directories(${target} PRIVATE "${FZERO_SHADER_DIR}")
        target_compile_definitions(${target} PRIVATE FZERO_VULKAN_SHADERS=1)
    endif()
endfunction()
