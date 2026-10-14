# libgit2 sert uniquement à lire le dépôt local (mode Diff) : pas de réseau, pas
# de SSH ni de HTTPS, et zlib embarqué pour rester sans dépendance système.

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

# libgit2 détecte `struct stat::st_mtim` en C90 strict, où glibc ne l'expose
# pas sans _DEFAULT_SOURCE : sans cette définition, la détection échoue et la
# compilation s'arrête sur « unknown struct stat nanosecond type ».
set(CMAKE_REQUIRED_DEFINITIONS -D_DEFAULT_SOURCE)
FetchContent_MakeAvailable(libgit2)
unset(CMAKE_REQUIRED_DEFINITIONS)

add_library(git2 INTERFACE)
target_link_libraries(git2 INTERFACE libgit2package)
target_include_directories(git2 INTERFACE ${libgit2_SOURCE_DIR}/include)
