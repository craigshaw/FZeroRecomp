cmake_minimum_required(VERSION 3.20)
project(fzero_recomp C CXX)

set(CMAKE_C_STANDARD 11)

set(SNESRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp" CACHE PATH
    "Path to the snesrecomp checkout used by this game")
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

if(NOT SNESRECOMP_SDL_BACKEND STREQUAL "SDL3")
    message(FATAL_ERROR "The F-Zero desktop host requires SDL3")
endif()
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH
    "Path to the pinned recomp-ui checkout")
include(${RECOMP_UI_ROOT}/recomp_ui.cmake)

file(GLOB GENERATED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.c")

add_executable(fzero_recomp
    ${SNESRECOMP_RUNNER_SOURCES}
    ${GENERATED_SOURCES}
    src/fzero_rtl.c
    src/fzero_spc_player.c
    src/main.c
    src/presentation.c
    src/fzero_layers.c
    src/fzero_vehicles.c
    src/fzero_ground.c
    src/config.c
    src/runtime_ui.c
    src/runtime_ui_imgui.cpp
    src/imgui_impl_sdlrenderer3.cpp
)
# The runner's debug hooks (debug_on_* / RDB_BLOCK_HOOK) are only compiled
# when SNESRECOMP_REVERSE_DEBUG is on; SMW's build forces it off for a
# normal release, as does this one.
target_compile_definitions(fzero_recomp PRIVATE SNESRECOMP_REVERSE_DEBUG=0)
target_include_directories(fzero_recomp PRIVATE
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    ${SNESRECOMP_RUNNER_ROOT}/src/desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
snesrecomp_target_sdl(fzero_recomp)
recomp_target_launcher_ui(fzero_recomp
    CONSOLE snes
    BOXART "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/boxart.png"
    BOXART_NAME boxart.png)
set_property(TARGET fzero_recomp APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher/boxart.png")
if(NOT MSVC)
    target_compile_options(fzero_recomp PRIVATE
        -w $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>)
endif()
target_link_libraries(fzero_recomp PRIVATE ${SNESRECOMP_RUNNER_LIBRARIES})

option(FZERO_MACOS_APP "Use per-user storage for the packaged Mac app" OFF)
if(FZERO_MACOS_APP)
    if(NOT APPLE)
        message(FATAL_ERROR "FZERO_MACOS_APP requires macOS")
    endif()
    target_compile_definitions(fzero_recomp PRIVATE FZERO_MACOS_APP=1)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(fzero_host_ui_tests
        tests/test_host_ui.c
        src/config.c src/runtime_ui.c src/runtime_ui_imgui.cpp
        src/presentation.c
        src/imgui_impl_sdlrenderer3.cpp
        ${RECOMP_UI_ROOT}/src/common/recomp_runtime_ui.c
        ${RECOMP_UI_ROOT}/src/common/recomp_runtime_settings.c
        ${RECOMP_UI_ROOT}/src/common/backends/imgui/runtime_ui_imgui.cpp
        ${RECOMP_UI_ROOT}/src/third_party/imgui/imgui.cpp
        ${RECOMP_UI_ROOT}/src/third_party/imgui/imgui_draw.cpp
        ${RECOMP_UI_ROOT}/src/third_party/imgui/imgui_tables.cpp
        ${RECOMP_UI_ROOT}/src/third_party/imgui/imgui_widgets.cpp
        ${RECOMP_UI_ROOT}/src/third_party/imgui/backends/imgui_impl_sdl3.cpp)
    target_include_directories(fzero_host_ui_tests PRIVATE src
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
        ${RECOMP_UI_ROOT}/src ${RECOMP_UI_ROOT}/src/common
        ${RECOMP_UI_ROOT}/src/third_party/imgui
        ${RECOMP_UI_ROOT}/src/third_party/imgui/backends)
    set_target_properties(fzero_host_ui_tests PROPERTIES CXX_STANDARD 17)
    snesrecomp_target_sdl(fzero_host_ui_tests)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host-ui-test-data")
    add_test(NAME fzero_host_ui COMMAND fzero_host_ui_tests)
    set_tests_properties(fzero_host_ui PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host-ui-test-data"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME fzero_wrong_rom
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rom_rejection.py"
            $<TARGET_FILE:fzero_recomp>)
    add_executable(fzero_layers_tests tests/test_layers.c tests/test_vehicles.c tests/test_ground.c
        src/fzero_layers.c src/fzero_vehicles.c src/fzero_ground.c
        ${SNESRECOMP_RUNNER_ROOT}/src/snes/ppu.c
        ${SNESRECOMP_RUNNER_ROOT}/src/snes/ppu_legacy.c)
    target_include_directories(fzero_layers_tests PRIVATE src ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_compile_definitions(fzero_layers_tests PRIVATE SNESRECOMP_REVERSE_DEBUG=0)
    add_test(NAME fzero_layers COMMAND fzero_layers_tests)
    add_executable(fzero_raster_tests tests/test_raster.c src/fzero_rtl.c)
    target_include_directories(fzero_raster_tests PRIVATE src config ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_compile_definitions(fzero_raster_tests PRIVATE SNESRECOMP_REVERSE_DEBUG=0)
    add_test(NAME fzero_raster COMMAND fzero_raster_tests)
endif()
