# Keep F-Zero's controls and save handling in the host. Apply the adapter to
# a build-local copy, leaving the pinned recomp-ui checkout untouched.
function(fzero_target_launcher target)
    find_package(Git REQUIRED)
    set(upstream "${RECOMP_UI_ROOT}/src/common/backends/imgui/launcher_imgui.cpp")
    set(adapter "${CMAKE_CURRENT_SOURCE_DIR}/patches/recomp-ui/0001-fzero-launcher-controls.patch")
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/fzero-launcher")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${upstream}" "${adapter}")
    file(MAKE_DIRECTORY "${directory}")
    file(READ "${upstream}" backend)
    string(REPLACE "\r\n" "\n" backend "${backend}")
    file(WRITE "${directory}/launcher_imgui.cpp" "${backend}")
    # git apply silently skips the ignored build-local file when it discovers
    # the source checkout above this directory. Stop repository discovery at
    # the build root so the patch applies to this copied file.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "GIT_CEILING_DIRECTORIES=${CMAKE_CURRENT_BINARY_DIR}"
        "${GIT_EXECUTABLE}" apply --no-index --whitespace=error "${adapter}"
        WORKING_DIRECTORY "${directory}" RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "F-Zero launcher adapter does not match recomp-ui: ${error}")
    endif()
    file(READ "${directory}/launcher_imgui.cpp" patched_backend)
    string(FIND "${patched_backend}" "FZeroSaveLocationSelect(" save_folder_marker)
    string(FIND "${patched_backend}" "fzero_launcher_display(" display_marker)
    if(save_folder_marker EQUAL -1 OR display_marker EQUAL -1)
        message(FATAL_ERROR "F-Zero launcher adapter was skipped; build-local launcher is unpatched")
    endif()
    get_target_property(sources ${target} SOURCES)
    list(REMOVE_ITEM sources "${upstream}")
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    target_sources(${target} PRIVATE "${directory}/launcher_imgui.cpp"
        src/launcher_settings.c src/launcher_saves.c)
endfunction()
