# Library containing the whole editor except main(): the executable and the
# tests link against it.
add_library(duet_core STATIC
    changes/ChangesView.cpp
    changes/DiffView.cpp
    changes/FileChangeDelegate.cpp
    changes/SnapshotsDialog.cpp
    claude/ClaudeActivity.cpp
    claude/ClaudeLauncher.cpp
    claude/ClaudeUsage.cpp
    claude/HookEvent.cpp
    claude/HookServer.cpp
    editor/ChangeGutter.cpp
    editor/CodeColorizer.cpp
    editor/EditorStatus.cpp
    editor/EditorTabs.cpp
    editor/EditorWidget.cpp
    editor/FileWatcher.cpp
    editor/FindBar.cpp
    editor/ImageView.cpp
    editor/MarkdownHtml.cpp
    editor/MarkdownView.cpp
    editor/TextFile.cpp
    git/CommitLog.cpp
    git/FilePatch.cpp
    git/FileStatuses.cpp
    git/GitCommands.cpp
    git/GitOperations.cpp
    git/GitProcess.cpp
    git/IgnoreRules.cpp
    git/LineChanges.cpp
    git/RepositoryStatus.cpp
    git/RepositoryWatcher.cpp
    git/Snapshots.cpp
    history/CommitDelegate.cpp
    history/HistoryView.cpp
    navigation/CodeNavigator.cpp
    navigation/Definitions.cpp
    navigation/GoToPopup.cpp
    navigation/NavigationHistory.cpp
    navigation/RecentFiles.cpp
    navigation/SymbolIndex.cpp
    outline/OutlineProviders.cpp
    outline/OutlineSymbol.cpp
    outline/OutlineView.cpp
    outline/TextOutlines.cpp
    outline/TreeSitterOutline.cpp
    search/FindInFiles.cpp
    search/FindInFilesPopup.cpp
    setup/Prerequisites.cpp
    setup/PrerequisitesDialog.cpp
    syntax/AppFonts.cpp
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
    workspace/CommandPalette.cpp
    workspace/FileIcons.cpp
    workspace/FileOperations.cpp
    workspace/FilesPanel.cpp
    workspace/FileTreeView.cpp
    workspace/OverlayPopup.cpp
    workspace/ProjectFiles.cpp
    workspace/ProjectTreeModel.cpp
    workspace/EntryPopups.cpp
    workspace/SidePanel.cpp
    workspace/ThemeStyle.cpp
    workspace/TitleBar.cpp
    workspace/TreeColumnFit.cpp
    workspace/TreeMetrics.cpp
    workspace/UiIcons.cpp
    workspace/WindowFrame.cpp
)

# The PTY depends on the system: ConPTY on Windows, forkpty elsewhere.
if(WIN32)
    target_sources(duet_core PRIVATE terminal/ConPty.cpp)
    target_compile_definitions(duet_core PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX)
else()
    target_sources(duet_core PRIVATE terminal/UnixPty.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(duet_core PRIVATE util) # forkpty() before glibc 2.34
    endif()
endif()

target_include_directories(duet_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(duet_core PUBLIC
    Qt6::Widgets Qt6::Network Qt6::WebEngineWidgets
    qscintilla tree_sitter_grammars vterm git2 cmark_gfm)
duet_add_queries(duet_core)

# GitHub's Markdown style sheets and icons, for the Markdown preview.
qt_add_resources(duet_core markdown_assets
    PREFIX /markdown
    BASE ${PROJECT_SOURCE_DIR}/third_party
    FILES
        ${PROJECT_SOURCE_DIR}/third_party/github-markdown-css/github-markdown-dark.css
        ${PROJECT_SOURCE_DIR}/third_party/github-markdown-css/github-markdown-light.css
        ${PROJECT_SOURCE_DIR}/third_party/octicons/info-16.svg
        ${PROJECT_SOURCE_DIR}/third_party/octicons/light-bulb-16.svg
        ${PROJECT_SOURCE_DIR}/third_party/octicons/report-16.svg
        ${PROJECT_SOURCE_DIR}/third_party/octicons/alert-16.svg
        ${PROJECT_SOURCE_DIR}/third_party/octicons/stop-16.svg
        ${PROJECT_SOURCE_DIR}/third_party/octicons/link-16.svg)

# Interface and code fonts, the same on every system.
qt_add_resources(duet_core fonts
    PREFIX /fonts
    BASE ${PROJECT_SOURCE_DIR}/third_party/fonts
    FILES
        ${PROJECT_SOURCE_DIR}/third_party/fonts/Inter-Variable.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-Regular.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-Italic.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-Bold.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-BoldItalic.ttf)

if(TARGET flowboard_ui)
    target_sources(duet_core PRIVATE editor/DiagramView.cpp)
    target_link_libraries(duet_core PRIVATE flowboard_ui PUBLIC Qt6::OpenGLWidgets)
    target_compile_definitions(duet_core PUBLIC DUET_WITH_DIAGRAMS)
endif()

if(MSVC)
    target_compile_options(duet_core PRIVATE /W4)
else()
    target_compile_options(duet_core PRIVATE -Wall -Wextra)
endif()

qt_add_executable(duet WIN32 MACOSX_BUNDLE app/main.cpp app/MainWindow.cpp)
target_link_libraries(duet PRIVATE duet_core)
target_compile_definitions(duet PRIVATE DUET_VERSION="${PROJECT_VERSION}")
set_target_properties(duet PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_dependencies(duet duet-hook)

# ClaudeLauncher looks for duet-hook next to the executable: on macOS,
# that is inside the bundle (Duet.app/Contents/MacOS).
if(APPLE)
    set_target_properties(duet PROPERTIES OUTPUT_NAME Duet)
    add_custom_command(TARGET duet POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:duet-hook> $<TARGET_FILE_DIR:duet>)
endif()

# "duet" component: the dependencies (libgit2) declare their own
# install rules, which we do not want in the packages.
#   cmake --install build --component duet --prefix <folder>
include(GNUInstallDirs)
install(TARGETS duet
    BUNDLE DESTINATION . COMPONENT duet
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT duet)
if(UNIX AND NOT APPLE)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/duet.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT duet)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/duet.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT duet)
endif()
