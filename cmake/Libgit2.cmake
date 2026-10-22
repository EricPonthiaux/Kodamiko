# libgit2 is only used to read the local repository (status, diffs, snapshots);
# writes go through the git command (see src/git/GitOperations.h). No
# network, no SSH or HTTPS, and bundled zlib to avoid any system
# dependency.

set(BUILD_TESTS        OFF CACHE BOOL "" FORCE)
set(BUILD_CLI          OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS      OFF CACHE BOOL "" FORCE)
set(USE_HTTPS          OFF CACHE STRING "" FORCE)
set(USE_SSH            OFF CACHE STRING "" FORCE)
set(USE_GSSAPI         OFF CACHE STRING "" FORCE)
set(USE_NTLMCLIENT     OFF CACHE BOOL "" FORCE)
set(USE_BUNDLED_ZLIB   ON  CACHE BOOL "" FORCE)
set(REGEX_BACKEND      builtin CACHE STRING "" FORCE)
set(USE_HTTP_PARSER    builtin CACHE STRING "" FORCE)
# MSVC only: the same C runtime as Qt and Duet (/MD), not libgit2's default /MT.
set(STATIC_CRT         OFF CACHE BOOL "" FORCE)

# libgit2 detects `struct stat::st_mtim` in strict C90, where glibc does not
# expose it without _DEFAULT_SOURCE: without this definition, detection fails and
# the build stops with "unknown struct stat nanosecond type".
set(CMAKE_REQUIRED_DEFINITIONS -D_DEFAULT_SOURCE)
FetchContent_MakeAvailable(libgit2)
unset(CMAKE_REQUIRED_DEFINITIONS)

add_library(git2 INTERFACE)
target_link_libraries(git2 INTERFACE libgit2package)
target_include_directories(git2 INTERFACE ${libgit2_SOURCE_DIR}/include)
