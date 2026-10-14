cmake_minimum_required(VERSION 3.24)

project(Enlumine
    VERSION 0.1.0
    DESCRIPTION "Éditeur de sources C/C++/Java/JavaScript avec terminal Claude intégré"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sources et messages en français : MSVC doit les lire en UTF-8.
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

# Toutes les dépendances tierces (hors Qt) sont compilées en statique depuis
# leurs sources : seul Qt 6 doit être installé sur la machine.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network PrintSupport Test)
qt_standard_project_setup()

include(cmake/Dependencies.cmake)

enable_testing()

add_subdirectory(src)
add_subdirectory(tools/enlumine-hook)
add_subdirectory(tests)
