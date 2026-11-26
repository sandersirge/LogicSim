cmake_minimum_required(VERSION 3.28)
project(LogicSim VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Programs (and Windows DLLs) go to <build>/bin, static libraries to <build>/lib
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(LOGICSIM_BUILD_GUI "Build the app (needs SFML and Dear ImGui); OFF builds only the core and tests" ON)
option(LOGICSIM_FETCH_SFML "Download and build SFML if SFML 3.1 is not installed" ON)

include(FetchContent)

# nlohmann/json (header only) for circuit files: the installed package or else downloaded.
find_package(nlohmann_json 3.11 QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "Using installed nlohmann_json ${nlohmann_json_VERSION}")
else()
    FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        SYSTEM)
    FetchContent_MakeAvailable(json)
endif()

if(LOGICSIM_BUILD_GUI)
    # SFML 3.1: the system package (MSYS2 mingw-w64-ucrt-x86_64-sfml, Homebrew sfml, apt libsfml-dev
    # when it is new enough) or else downloaded and built, so the project also builds on a bare machine.
    find_package(SFML 3.1 QUIET COMPONENTS Graphics Window System)
    if(SFML_FOUND)
        message(STATUS "Using installed SFML ${SFML_VERSION}")
    elseif(LOGICSIM_FETCH_SFML)
        message(STATUS "SFML 3.1 not found; downloading and building it (first configure only)")
        FetchContent_Declare(SFML
            GIT_REPOSITORY https://github.com/SFML/SFML.git
            GIT_TAG 3.1.0
            GIT_SHALLOW ON
            EXCLUDE_FROM_ALL
            SYSTEM)
        set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
        set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(SFML)
    else()
        message(FATAL_ERROR "SFML 3.1 not found. Install it or configure with -DLOGICSIM_FETCH_SFML=ON.")
    endif()

    # Dear ImGui (no CMake project of its own) and its SFML backend, ImGui-SFML. Pinned to the
    # newest pair that is released and works together.
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9b
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(imgui)
    set(IMGUI_DIR ${imgui_SOURCE_DIR})
    set(IMGUI_SFML_IMGUI_DEMO OFF CACHE BOOL "" FORCE)
    # The SFML targets exist already (found or downloaded above), so ImGui-SFML must not search
    # again: its own find_package(SFML) fails with a CMake that does not search the MSYS2 prefix
    # (e.g. the CMake bundled with CLion), on SFML's FreeType dependency.
    set(IMGUI_SFML_FIND_SFML OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(ImGui-SFML
        GIT_REPOSITORY https://github.com/SFML/imgui-sfml.git
        GIT_TAG v3.0
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
        SYSTEM)
    FetchContent_MakeAvailable(ImGui-SFML)
    # ImGui-SFML 3.0 still calls touch functions that SFML 3.1 marks deprecated
    if(NOT MSVC)
        target_compile_options(ImGui-SFML PRIVATE -Wno-deprecated-declarations)
    endif()
endif()

# Warnings for every target of this project (not for downloaded libraries)
function(logicsim_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# On Windows copy the DLLs next to an executable so it starts from anywhere.
function(copy_runtime_dlls target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E
                    $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${target}>>,copy_if_different,true>
                    $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS)
    endif()
endfunction()

# Signals, components, circuits, the netlist compiler, the simulator and JSON files (no SFML or
# ImGui): used by the app and the tests.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(logicsim_core STATIC ${CORE_SOURCES})
target_include_directories(logicsim_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_link_libraries(logicsim_core PUBLIC nlohmann_json::nlohmann_json)
logicsim_warnings(logicsim_core)

if(LOGICSIM_BUILD_GUI)
    file(GLOB GUI_SOURCES CONFIGURE_DEPENDS src/gui/*.cpp)
    # ImGui's std::string helpers (InputText with std::string) are not part of ImGui-SFML
    add_executable(logicsim src/main.cpp ${GUI_SOURCES} ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
    target_include_directories(logicsim PRIVATE ${imgui_SOURCE_DIR}/misc/cpp)
    target_link_libraries(logicsim PRIVATE logicsim_core ImGui-SFML::ImGui-SFML SFML::Graphics)
    logicsim_warnings(logicsim)
    if(MINGW)
        # A GUI application: no console window next to it
        target_link_options(logicsim PRIVATE -mwindows)
    endif()
    copy_runtime_dlls(logicsim)
    # The example circuits next to the program, so File > Examples finds them
    add_custom_command(TARGET logicsim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/examples $<TARGET_FILE_DIR:logicsim>/examples)
endif()

# Tests (no window needed): ctest --test-dir <build folder>
include(CTest)
if(BUILD_TESTING)
    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*_test.cpp)
    foreach(source ${TEST_SOURCES})
        get_filename_component(name ${source} NAME_WE)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE logicsim_core)
        target_compile_definitions(${name} PRIVATE
            LOGICSIM_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")
        logicsim_warnings(${name})
        add_test(NAME ${name} COMMAND ${name})
    endforeach()
    if(LOGICSIM_BUILD_GUI)
        # Editing through the editor's mouse and keyboard handlers (no window is opened)
        add_executable(editor_test tests/gui/editor_test.cpp src/gui/editor.cpp src/gui/geometry.cpp
                       src/gui/draw.cpp ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
        target_include_directories(editor_test PRIVATE ${imgui_SOURCE_DIR}/misc/cpp ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(editor_test PRIVATE logicsim_core ImGui-SFML::ImGui-SFML SFML::Graphics)
        logicsim_warnings(editor_test)
        copy_runtime_dlls(editor_test)
        add_test(NAME editor_test COMMAND editor_test)
    endif()
endif()
