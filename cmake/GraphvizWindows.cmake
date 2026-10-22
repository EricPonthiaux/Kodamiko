# Graphviz on Windows: the official release, downloaded at configure time.
# It is the build Graphviz tests and ships (MSVC, Release, shared libraries);
# building it here would need Bison, Flex and getopt, and its static build is
# only tested in Debug upstream.
#
#   Graphviz   graph layout (diagram editor)   EPL
#
# Sets GRAPHVIZ_FOUND, GRAPHVIZ_LIBRARIES, the plugins' import libraries
# (GVPLUGIN_core, GVPLUGIN_dot_layout, GVPLUGIN_neato_layout) and
# GRAPHVIZ_RUNTIME_DLLS, the DLLs the executable loads, copied next to it.

set(GRAPHVIZ_WINDOWS_VERSION 16.1.0)
FetchContent_Declare(graphviz
    URL https://gitlab.com/api/v4/projects/4207231/packages/generic/graphviz-releases/${GRAPHVIZ_WINDOWS_VERSION}/windows_10_cmake_Release_Graphviz-${GRAPHVIZ_WINDOWS_VERSION}-win64.zip
    URL_HASH SHA256=733e49626c492242eb8dca30ea627b6ead20710e207998c7933b4909d92d6abc
    SOURCE_SUBDIR ${DOWNLOAD_ONLY})
FetchContent_MakeAvailable(graphviz)

find_package(graphviz CONFIG REQUIRED PATHS ${graphviz_SOURCE_DIR} NO_DEFAULT_PATH)
# The release's gvc target links zlib.lib by a path of Graphviz's build
# machine: Duet does not call zlib, gvc.dll loads zlib1.dll by itself.
set_property(TARGET graphviz::gvc PROPERTY INTERFACE_LINK_LIBRARIES graphviz::cgraph)
# GVDLL: the headers declare the functions imported from the DLLs.
set_property(TARGET graphviz::gvc graphviz::cgraph PROPERTY INTERFACE_COMPILE_DEFINITIONS GVDLL)
set(GRAPHVIZ_LIBRARIES graphviz::gvc graphviz::cgraph)
set(GRAPHVIZ_FOUND TRUE)

foreach(plugin core dot_layout neato_layout)
    find_library(GVPLUGIN_${plugin} gvplugin_${plugin}
        PATHS ${graphviz_SOURCE_DIR}/lib NO_DEFAULT_PATH REQUIRED)
endforeach()

# What the libraries and the three plugins load, transitively (objdump -p on
# the 16.1.0 release); the MSVC runtime comes with Qt's. To check again after
# a version change: a missing DLL makes every test fail to start.
set(graphviz_dlls
    gvc cgraph cdt pathplan xdot
    gvplugin_core gvplugin_dot_layout gvplugin_neato_layout
    libexpat zlib1
    gts glib-2.0-0 iconv-2 intl-8 pcre2-8)
set(GRAPHVIZ_RUNTIME_DLLS "")
foreach(dll ${graphviz_dlls})
    list(APPEND GRAPHVIZ_RUNTIME_DLLS ${graphviz_SOURCE_DIR}/bin/${dll}.dll)
endforeach()
# Next to duet.exe in the build tree too, where the tests find them.
file(COPY ${GRAPHVIZ_RUNTIME_DLLS} DESTINATION ${CMAKE_BINARY_DIR}/bin)
