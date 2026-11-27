cmake_minimum_required(VERSION 3.28)
project(LogicSim VERSION 1.0.0
    DESCRIPTION "Logic-gate simulator: place gates, wire them and watch the signals propagate"
    HOMEPAGE_URL "https://github.com/sandersirge/LogicSim"
    LANGUAGES CXX)

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
option(LOGICSIM_MACOS_BUNDLE "macOS: build LogicSim.app instead of a plain logicsim program" OFF)
option(LOGICSIM_SANITIZE "Build this project's targets with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(LOGICSIM_SANITIZE AND MSVC)
    # MSVC refuses /fsanitize=address together with the run-time checks of its default Debug flags
    string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

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

# Warnings (and sanitizers when enabled) for every target of this project, not for downloaded libraries
function(logicsim_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(LOGICSIM_SANITIZE)
        if(MSVC)
            target_compile_options(${target} PRIVATE /fsanitize=address)
        else()
            # Every report stops the program, so a test with undefined behaviour fails;
            # _GLIBCXX_ASSERTIONS adds bounds checks to std::vector::operator[] and friends
            set(flags -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
            target_compile_options(${target} PRIVATE ${flags})
            target_compile_definitions(${target} PRIVATE _GLIBCXX_ASSERTIONS)
            target_link_options(${target} PUBLIC ${flags})
        endif()
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
# version.hpp: the version, description and homepage from project() above
configure_file(cmake/version.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/core/version.hpp @ONLY)
target_include_directories(logicsim_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc
                           ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(logicsim_core PUBLIC nlohmann_json::nlohmann_json)
logicsim_warnings(logicsim_core)

if(LOGICSIM_BUILD_GUI)
    file(GLOB GUI_SOURCES CONFIGURE_DEPENDS src/gui/*.cpp)
    # ImGui's std::string helpers (InputText with std::string) are not part of ImGui-SFML
    add_executable(logicsim src/main.cpp ${GUI_SOURCES} ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
    target_include_directories(logicsim PRIVATE ${imgui_SOURCE_DIR}/misc/cpp)
    target_link_libraries(logicsim PRIVATE logicsim_core ImGui-SFML::ImGui-SFML SFML::Graphics)
    logicsim_warnings(logicsim)
    if(WIN32)
        # Windows: icon and version information as resources of logicsim.exe (the icon in res/ is
        # made by the logicsim_icons tool below)
        enable_language(RC)
        set(LOGICSIM_ICO ${CMAKE_CURRENT_SOURCE_DIR}/res/logicsim.ico)
        configure_file(cmake/logicsim.rc.in ${CMAKE_CURRENT_BINARY_DIR}/generated/logicsim.rc @ONLY)
        target_sources(logicsim PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated/logicsim.rc)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS res/logicsim.ico)
    endif()
    set(EXAMPLES_DESTINATION $<TARGET_FILE_DIR:logicsim>/examples)
    if(APPLE AND LOGICSIM_MACOS_BUNDLE)
        # macOS: <build>/bin/LogicSim.app with the icon, an Info.plist and the examples in Resources
        set(ICNS ${CMAKE_CURRENT_SOURCE_DIR}/res/logicsim.icns)
        set_source_files_properties(${ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(logicsim PRIVATE ${ICNS})
        set_target_properties(logicsim PROPERTIES
            MACOSX_BUNDLE TRUE
            OUTPUT_NAME "LogicSim"
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in
            MACOSX_BUNDLE_EXECUTABLE_NAME "LogicSim"
            MACOSX_BUNDLE_BUNDLE_NAME "LogicSim"
            MACOSX_BUNDLE_GUI_IDENTIFIER io.github.sandersirge.logicsim
            MACOSX_BUNDLE_ICON_FILE logicsim.icns
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_COPYRIGHT "${PROJECT_HOMEPAGE_URL}")
        set(EXAMPLES_DESTINATION $<TARGET_BUNDLE_CONTENT_DIR:logicsim>/Resources/examples)
    endif()
    if(MINGW)
        # A GUI application: no console window next to it
        target_link_options(logicsim PRIVATE -mwindows)
    endif()
    copy_runtime_dlls(logicsim)
    # The example circuits next to the program (in the bundle's Resources on macOS), so File > Examples
    # finds them
    add_custom_command(TARGET logicsim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/examples ${EXAMPLES_DESTINATION})

    # Writes res/logicsim.png, .ico and .icns from the logo drawn in code (not built by default:
    # make icons, or build it and run logicsim_icons res)
    add_executable(logicsim_icons EXCLUDE_FROM_ALL tools/make_icons.cpp src/gui/logo.cpp)
    target_include_directories(logicsim_icons PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
    target_link_libraries(logicsim_icons PRIVATE SFML::Graphics)
    logicsim_warnings(logicsim_icons)
    copy_runtime_dlls(logicsim_icons)
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
        target_compile_definitions(editor_test PRIVATE
            LOGICSIM_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")
        logicsim_warnings(editor_test)
        copy_runtime_dlls(editor_test)
        add_test(NAME editor_test COMMAND editor_test)
        # The logo drawing (plain pixels, no window)
        add_executable(logo_test tests/gui/logo_test.cpp src/gui/logo.cpp)
        target_include_directories(logo_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        logicsim_warnings(logo_test)
        add_test(NAME logo_test COMMAND logo_test)
    endif()
endif()
