# Bibliothèque contenant tout l'éditeur sauf main() : l'exécutable et les
# tests s'y lient.
add_library(enlumine_core STATIC
    changes/DiffView.cpp
    changes/WorkingTreeDiff.cpp
    claude/ClaudeLauncher.cpp
    claude/HookEvent.cpp
    claude/HookServer.cpp
    editor/EditorTabs.cpp
    editor/EditorWidget.cpp
    editor/FileWatcher.cpp
    editor/ImageView.cpp
    editor/TextFile.cpp
    outline/OutlineProviders.cpp
    outline/OutlineSymbol.cpp
    outline/OutlineView.cpp
    outline/TextOutlines.cpp
    outline/TreeSitterOutline.cpp
    syntax/HighlighterFactory.cpp
    syntax/HighlightQuery.cpp
    syntax/LanguageRegistry.cpp
    syntax/LexillaHighlighter.cpp
    syntax/PlainTextHighlighter.cpp
    syntax/ScintillaStyles.cpp
    syntax/Theme.cpp
    syntax/TreeSitterGrammars.cpp
    syntax/TreeSitterHighlighter.cpp
    terminal/IPty.h
    terminal/ProcessSpec.cpp
    terminal/TerminalWidget.cpp
    terminal/VtScreen.cpp
    workspace/FileTreeView.cpp
)

# Le PTY dépend du système : ConPTY sous Windows, forkpty ailleurs.
if(WIN32)
    target_sources(enlumine_core PRIVATE terminal/ConPty.cpp)
    target_compile_definitions(enlumine_core PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX)
else()
    target_sources(enlumine_core PRIVATE terminal/UnixPty.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(enlumine_core PRIVATE util) # forkpty() avant glibc 2.34
    endif()
endif()

target_include_directories(enlumine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(enlumine_core PUBLIC
    Qt6::Widgets Qt6::Network
    qscintilla tree_sitter_grammars vterm git2)
enlumine_add_queries(enlumine_core)

if(MSVC)
    target_compile_options(enlumine_core PRIVATE /W4)
else()
    target_compile_options(enlumine_core PRIVATE -Wall -Wextra)
endif()

qt_add_executable(enlumine WIN32 MACOSX_BUNDLE app/main.cpp app/MainWindow.cpp)
target_link_libraries(enlumine PRIVATE enlumine_core)
target_compile_definitions(enlumine PRIVATE ENLUMINE_VERSION="${PROJECT_VERSION}")
set_target_properties(enlumine PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_dependencies(enlumine enlumine-hook)

# ClaudeLauncher cherche enlumine-hook à côté de l'exécutable : sous macOS,
# c'est dans le bundle (Enlumine.app/Contents/MacOS).
if(APPLE)
    set_target_properties(enlumine PROPERTIES OUTPUT_NAME Enlumine)
    add_custom_command(TARGET enlumine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:enlumine-hook> $<TARGET_FILE_DIR:enlumine>)
endif()

# Composant « enlumine » : les dépendances (libgit2) déclarent leurs propres
# règles d'installation, qu'on ne veut pas dans les paquets.
#   cmake --install build --component enlumine --prefix <dossier>
include(GNUInstallDirs)
install(TARGETS enlumine
    BUNDLE DESTINATION . COMPONENT enlumine
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT enlumine)
if(UNIX AND NOT APPLE)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/enlumine.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT enlumine)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/enlumine.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT enlumine)
endif()
