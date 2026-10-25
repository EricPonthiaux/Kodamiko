# Third-party dependencies, downloaded at configure time and built statically.
#
#   QScintilla   editing component (Scintilla for Qt)           GPLv3
#   tree-sitter  incremental parsing + grammars                 MIT
#   libvterm     VT220/xterm terminal emulation                 MIT
#   libgit2      git repository reading (Diff mode)             GPLv2 + linking exception
#   cmark-gfm    GitHub's Markdown parser (Markdown preview)    BSD-2-Clause

include(FetchContent)
# Downloads print their progress: a quiet configure looks stuck while git
# clones (FetchContent is quiet by default).
set(FETCHCONTENT_QUIET OFF)
# A dependency already downloaded is not checked against its repository
# again: configuring needs no network. Changing its GIT_TAG still fetches
# the new version.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# Pointing SOURCE_SUBDIR at a nonexistent folder makes FetchContent
# download the project without adding it to the build: we define its
# targets ourselves (files included at the end), simpler than its own.
set(DOWNLOAD_ONLY "_download_only_")

FetchContent_Declare(qscintilla
    URL https://www.riverbankcomputing.com/static/Downloads/QScintilla/2.14.1/QScintilla_src-2.14.1.tar.gz
    URL_HASH SHA256=dfe13c6acc9d85dfcba76ccc8061e71a223957a6c02f3c343b30a9d43a4cdd4d
    DOWNLOAD_EXTRACT_TIMESTAMP ON)

FetchContent_Declare(tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG v0.26.13
    GIT_SHALLOW ON
    SOURCE_SUBDIR ${DOWNLOAD_ONLY})

FetchContent_Declare(tree_sitter_c
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-c.git
    GIT_TAG v0.24.2
    GIT_SHALLOW ON
    SOURCE_SUBDIR ${DOWNLOAD_ONLY})

FetchContent_Declare(tree_sitter_cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG v0.23.4
    GIT_SHALLOW ON
    SOURCE_SUBDIR ${DOWNLOAD_ONLY})

FetchContent_Declare(tree_sitter_java
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-java.git
    GIT_TAG v0.23.5
    GIT_SHALLOW ON
    SOURCE_SUBDIR ${DOWNLOAD_ONLY})

FetchContent_Declare(tree_sitter_javascript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-javascript.git
    GIT_TAG v0.25.0
    GIT_SHALLOW ON
    SOURCE_SUBDIR ${DOWNLOAD_ONLY})

FetchContent_Declare(tree_sitter_typescript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-typescript.git
    GIT_TAG v0.23.2
    GIT_SHALLOW ON
    SOURCE_SUBDIR ${DOWNLOAD_ONLY})

FetchContent_Declare(tree_sitter_python
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python.git
    GIT_TAG v0.25.0
    GIT_SHALLOW ON
    SOURCE_SUBDIR ${DOWNLOAD_ONLY})

FetchContent_Declare(libvterm
    GIT_REPOSITORY https://github.com/neovim/libvterm.git
    GIT_TAG v0.3.3
    GIT_SHALLOW ON
    SOURCE_SUBDIR ${DOWNLOAD_ONLY})

FetchContent_Declare(libgit2
    GIT_REPOSITORY https://github.com/libgit2/libgit2.git
    GIT_TAG v1.9.7
    GIT_SHALLOW ON)

FetchContent_Declare(cmark_gfm
    GIT_REPOSITORY https://github.com/github/cmark-gfm.git
    GIT_TAG 0.29.0.gfm.13
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL) # only its libraries are built, not its command

FetchContent_MakeAvailable(qscintilla tree_sitter tree_sitter_c tree_sitter_cpp tree_sitter_java
                           tree_sitter_javascript tree_sitter_typescript tree_sitter_python libvterm)

include(${CMAKE_CURRENT_LIST_DIR}/QScintilla.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/TreeSitter.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Libvterm.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Libgit2.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/CmarkGfm.cmake)
