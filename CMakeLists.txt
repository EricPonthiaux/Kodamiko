cmake_minimum_required(VERSION 3.24)

project(Kodamiko
    VERSION 1.0.1
    DESCRIPTION "C/C++/Java/JavaScript source editor with an integrated Claude terminal"
    LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX) # the AppKit calls Qt has no API for
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# clangd finds the compile commands at the root of the sources or in build/,
# not in the other presets' folders (build-win, build-debug...): each build
# copies its own to the root (ignored by git), the last one built wins.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    add_custom_target(compile-commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_SOURCE_DIR}/compile_commands.json
        COMMENT "Copying compile_commands.json to the source root for clangd"
        VERBATIM)
endif()

# Sources contain non-ASCII characters: MSVC must read them as UTF-8.
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

# All third-party dependencies (except Qt) are built statically from
# their sources: only Qt 6 needs to be installed on the machine.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Widgets Network PrintSupport Test WebEngineWidgets)
qt_standard_project_setup()

# Link-time optimization across the application and the statically built
# dependencies (QScintilla, tree-sitter, libvterm, libgit2, cmark-gfm).
option(KDK_LTO "Build with link-time optimization" OFF)
if(KDK_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT kdk_ipo_supported OUTPUT kdk_ipo_error LANGUAGES C CXX)
    if(kdk_ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        # Also for the dependencies that declare an old cmake_minimum_required.
        set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
    else()
        message(WARNING "Link-time optimization unavailable: ${kdk_ipo_error}")
    endif()
endif()

# Code generated for the processor of the building machine (AVX2...): faster,
# but the binary may not run on an older processor. For local builds only,
# never for packages meant for other machines.
option(KDK_NATIVE "Optimize for the building machine's processor (-march=native)" OFF)
if(KDK_NATIVE)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-march=native kdk_native_supported)
    if(kdk_native_supported)
        # Before the dependencies: they are built with it too. At link time
        # as well, where link-time optimization generates the code.
        add_compile_options(-march=native)
        add_link_options(-march=native)
    else()
        message(WARNING "-march=native is not supported by this compiler")
    endif()
endif()

include(cmake/Dependencies.cmake)
include(cmake/CodeQuality.cmake)
include(cmake/RelinkWhileRunning.cmake)

# Diagram editor (Mermaid, PlantUML, explainer boards): its layouts come from
# Graphviz's library. Without it, diagram files open as text. Both branches
# set GRAPHVIZ_FOUND, GRAPHVIZ_LIBRARIES and the plugins Kodamiko links in rather
# than loads from Graphviz's plugin configuration (GVPLUGIN_core,
# GVPLUGIN_dot_layout, GVPLUGIN_neato_layout): the same engines everywhere,
# and no text measured with system fonts (as in sidecar's WebAssembly build).
if(WIN32)
    include(cmake/GraphvizWindows.cmake)
else()
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GRAPHVIZ IMPORTED_TARGET libgvc libcgraph)
    endif()
    if(GRAPHVIZ_FOUND)
        set(GRAPHVIZ_LIBRARIES PkgConfig::GRAPHVIZ)
        pkg_get_variable(GRAPHVIZ_LIBDIR libgvc libdir)
        foreach(plugin core dot_layout neato_layout)
            find_library(GVPLUGIN_${plugin} gvplugin_${plugin}
                HINTS ${GRAPHVIZ_LIBDIR} PATH_SUFFIXES graphviz)
        endforeach()
    endif()
endif()
if(NOT GRAPHVIZ_FOUND)
    message(STATUS "Graphviz (libgvc) not found: diagram files open as text")
endif()
find_package(Qt6 OPTIONAL_COMPONENTS Svg)

enable_testing()

add_subdirectory(src)
add_subdirectory(tools/kodamiko-hook)
add_subdirectory(tools/kodamiko-askpass)
add_subdirectory(tools/kodamiko-mcp)
add_subdirectory(tools/claude-plugin)
add_subdirectory(tools/recipes)
add_subdirectory(tools/bench-index)
add_subdirectory(tests)
