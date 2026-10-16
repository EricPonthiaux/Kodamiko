cmake_minimum_required(VERSION 3.24)

project(Enlumine
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

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network PrintSupport Test WebEngineWidgets)
qt_standard_project_setup()

include(cmake/Dependencies.cmake)

# Diagram editor for .flow files (Flowboard, Dear ImGui), built from its
# sources when they are available. Without it, .flow files open as text.
set(FLOWBOARD_SOURCE_DIR "" CACHE PATH "Flowboard sources: enables the diagram editor tabs")
if(FLOWBOARD_SOURCE_DIR)
    find_package(Qt6 REQUIRED COMPONENTS OpenGL OpenGLWidgets)
    add_subdirectory(${FLOWBOARD_SOURCE_DIR} flowboard EXCLUDE_FROM_ALL)
endif()

enable_testing()

add_subdirectory(src)
add_subdirectory(tools/enlumine-hook)
add_subdirectory(tests)
