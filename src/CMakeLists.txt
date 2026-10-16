# Library containing the whole editor except main(): the executable and the
# tests link against it.
add_library(enlumine_core STATIC
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
    target_sources(enlumine_core PRIVATE terminal/ConPty.cpp)
    target_compile_definitions(enlumine_core PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX)
else()
    target_sources(enlumine_core PRIVATE terminal/UnixPty.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(enlumine_core PRIVATE util) # forkpty() before glibc 2.34
    endif()
endif()

target_include_directories(enlumine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(enlumine_core PUBLIC
    Qt6::Widgets Qt6::Network Qt6::WebEngineWidgets
    qscintilla tree_sitter_grammars vterm git2 cmark_gfm)
enlumine_add_queries(enlumine_core)

# GitHub's Markdown style sheets and icons, for the Markdown preview.
qt_add_resources(enlumine_core markdown_assets
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
qt_add_resources(enlumine_core fonts
    PREFIX /fonts
    BASE ${PROJECT_SOURCE_DIR}/third_party/fonts
    FILES
        ${PROJECT_SOURCE_DIR}/third_party/fonts/Inter-Variable.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-Regular.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-Italic.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-Bold.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-BoldItalic.ttf)

if(TARGET flowboard_ui)
    target_sources(enlumine_core PRIVATE editor/DiagramView.cpp)
    target_link_libraries(enlumine_core PRIVATE flowboard_ui PUBLIC Qt6::OpenGLWidgets)
    target_compile_definitions(enlumine_core PUBLIC ENLUMINE_WITH_DIAGRAMS)
endif()

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

# ClaudeLauncher looks for enlumine-hook next to the executable: on macOS,
# that is inside the bundle (Enlumine.app/Contents/MacOS).
if(APPLE)
    set_target_properties(enlumine PROPERTIES OUTPUT_NAME Enlumine)
    add_custom_command(TARGET enlumine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:enlumine-hook> $<TARGET_FILE_DIR:enlumine>)
endif()

# "enlumine" component: the dependencies (libgit2) declare their own
# install rules, which we do not want in the packages.
#   cmake --install build --component enlumine --prefix <folder>
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
