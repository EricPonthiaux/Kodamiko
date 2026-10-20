cmake_minimum_required(VERSION 3.24)

project(Duet
    VERSION 0.1.0
    DESCRIPTION "C/C++/Java/JavaScript source editor with an integrated Claude terminal"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
option(DUET_LTO "Build with link-time optimization" OFF)
if(DUET_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT duet_ipo_supported OUTPUT duet_ipo_error LANGUAGES C CXX)
    if(duet_ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        # Also for the dependencies that declare an old cmake_minimum_required.
        set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
    else()
        message(WARNING "Link-time optimization unavailable: ${duet_ipo_error}")
    endif()
endif()

# Code generated for the processor of the building machine (AVX2...): faster,
# but the binary may not run on an older processor. For local builds only,
# never for packages meant for other machines.
option(DUET_NATIVE "Optimize for the building machine's processor (-march=native)" OFF)
if(DUET_NATIVE)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-march=native duet_native_supported)
    if(duet_native_supported)
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

# Diagram editor (Mermaid, PlantUML, explainer boards): its layouts come from
# Graphviz's library. Without it, diagram files open as text.
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GRAPHVIZ IMPORTED_TARGET libgvc libcgraph)
endif()
if(GRAPHVIZ_FOUND)
    # The layout and output plugins Duet uses, linked in rather than loaded
    # from Graphviz's plugin configuration: the same engines everywhere, and
    # no text measured with system fonts (as in sidecar's WebAssembly build).
    pkg_get_variable(GRAPHVIZ_LIBDIR libgvc libdir)
    foreach(plugin core dot_layout neato_layout)
        find_library(GVPLUGIN_${plugin} gvplugin_${plugin}
            HINTS ${GRAPHVIZ_LIBDIR} PATH_SUFFIXES graphviz)
    endforeach()
else()
    message(STATUS "Graphviz (libgvc) not found: diagram files open as text")
endif()
find_package(Qt6 OPTIONAL_COMPONENTS Svg)

enable_testing()

add_subdirectory(src)
add_subdirectory(tools/duet-hook)
add_subdirectory(tools/duet-askpass)
add_subdirectory(tools/duet-mcp)
add_subdirectory(tools/claude-plugin)
add_subdirectory(tests)
