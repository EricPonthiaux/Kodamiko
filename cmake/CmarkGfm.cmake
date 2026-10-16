# cmark-gfm, the parser GitHub renders Markdown with, and its extensions
# (tables, strikethrough, autolinks, task lists, tag filter): the Markdown
# preview reads files exactly as GitHub does. Static libraries only.

set(CMARK_TESTS     OFF CACHE BOOL "" FORCE)
set(CMARK_SHARED    OFF CACHE BOOL "" FORCE)
set(CMARK_STATIC    ON  CACHE BOOL "" FORCE)
set(CMARK_LIB_FUZZER OFF CACHE BOOL "" FORCE)
# Its CMakeLists asks for a CMake version that CMake 4 no longer supports,
# and libgit2 caches CMAKE_C_STANDARD 90 while cmark-gfm is written in C99.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_C_STANDARD 99)
FetchContent_MakeAvailable(cmark_gfm)
unset(CMAKE_C_STANDARD)
unset(CMAKE_POLICY_VERSION_MINIMUM)

add_library(cmark_gfm INTERFACE)
target_link_libraries(cmark_gfm INTERFACE libcmark-gfm-extensions_static libcmark-gfm_static)
target_compile_definitions(cmark_gfm INTERFACE CMARK_GFM_STATIC_DEFINE CMARK_GFM_EXTENSIONS_STATIC_DEFINE)
target_include_directories(cmark_gfm INTERFACE
    ${cmark_gfm_SOURCE_DIR}/src ${cmark_gfm_SOURCE_DIR}/extensions
    ${cmark_gfm_BINARY_DIR}/src ${cmark_gfm_BINARY_DIR}/extensions)
