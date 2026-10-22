# Library containing the whole editor except main(): the executable and the
# tests link against it.
add_library(duet_core STATIC
    agent/AgentActivity.cpp
    agent/AgentCheckPopup.cpp
    agent/AgentIndicator.cpp
    agent/AgentMergeQueue.cpp
    agent/AgentProviders.cpp
    agent/AgentSessions.cpp
    agent/CloseAgentPopup.cpp
    agent/CommitMessage.cpp
    agent/DiagramRequest.cpp
    agent/DiagramRequestPopup.cpp
    agent/EditorContext.cpp
    agent/HookServer.cpp
    agent/mcp/CodeTools.cpp
    agent/mcp/EditorTools.cpp
    agent/mcp/GitTools.cpp
    agent/mcp/McpRegistry.cpp
    agent/mcp/McpSession.cpp
    agent/mcp/RefactoringTools.cpp
    agent/mcp/TerminalTools.cpp
    agent/mcp/WorkspaceTools.cpp
    agent/NewAgentPopup.cpp
    agent/vendor/claude/ClaudeCodeProvider.cpp
    agent/vendor/claude/ClaudeConfig.cpp
    agent/vendor/claude/ClaudeConfigView.cpp
    agent/vendor/claude/ClaudeFileRun.cpp
    agent/vendor/claude/ClaudeLauncher.cpp
    agent/vendor/claude/ClaudeLint.cpp
    agent/vendor/claude/ClaudeLinter.cpp
    agent/vendor/claude/ClaudeMark.cpp
    agent/vendor/claude/ClaudeTrust.cpp
    agent/vendor/claude/ClaudeUsage.cpp
    agent/vendor/claude/CodeRequest.cpp
    agent/vendor/claude/EffectiveSettings.cpp
    agent/vendor/claude/UnsavedEditGuard.cpp
    changes/BranchesPopup.cpp
    changes/ChangeTree.cpp
    changes/ChangesView.cpp
    changes/ChangesViewDiff.cpp
    changes/ChangesViewList.cpp
    changes/CommitPanel.cpp
    changes/DiffHeaderBar.cpp
    changes/DiffLayout.cpp
    changes/SideBySide.cpp
    changes/SideBySideDivider.cpp
    changes/DiffStyles.cpp
    changes/DiffSyntaxHighlighter.cpp
    changes/DiffView.cpp
    changes/DiffViewSideBySide.cpp
    changes/DiscardConfirmation.cpp
    changes/FileChangeDelegate.cpp
    changes/GitActions.cpp
    changes/HunkActions.cpp
    changes/IdentityPopup.cpp
    changes/NewBranchPopup.cpp
    changes/ScratchEditors.cpp
    changes/SnapshotsPopup.cpp
    changes/StashChangesPopup.cpp
    changes/StashesPopup.cpp
    changes/TextPatch.cpp
    changes/WordDiff.cpp
    completion/CodeGeneration.cpp
    completion/CodeScan.cpp
    completion/CompletionController.cpp
    completion/CompletionHistory.cpp
    completion/CompletionPopup.cpp
    completion/CompletionService.cpp
    completion/DocComments.cpp
    completion/ExpectedType.cpp
    completion/FullLine.cpp
    completion/FuzzyMatch.cpp
    completion/LocalCompletion.cpp
    completion/NameSuggestions.cpp
    completion/Snippet.cpp
    completion/SnippetSession.cpp
    completion/Templates.cpp
    diagnostics/JsonSchema.cpp
    diagnostics/JsonTree.cpp
    diagnostics/ProblemStore.cpp
    diagnostics/ProblemsView.cpp
    editor/ChangeGutter.cpp
    editor/CodeColorizer.cpp
    editor/ClangFormatStyle.cpp
    editor/CodeFormatter.cpp
    editor/EditorStatus.cpp
    editor/EditorTabs.cpp
    editor/EditorWidget.cpp
    editor/FileWatcher.cpp
    editor/FindBar.cpp
    editor/MarkdownHtml.cpp
    editor/MarkdownToolTip.cpp
    editor/PreviewPage.cpp
    editor/PreviewSite.cpp
    editor/PreviewTab.cpp
    editor/ScrollBarMarks.cpp
    editor/SymbolOccurrences.cpp
    editor/TextFile.cpp
    git/Askpass.cpp
    git/Branches.cpp
    git/CommitLog.cpp
    git/ContentMatcher.cpp
    git/CredentialCache.cpp
    git/FilePatch.cpp
    git/FileStatuses.cpp
    git/GitCommands.cpp
    git/GitOperations.cpp
    git/GitProcess.cpp
    git/IgnoreRules.cpp
    git/Integration.cpp
    git/LineChanges.cpp
    git/Remotes.cpp
    git/RepositoryStatus.cpp
    git/RepositoryWatcher.cpp
    git/Snapshots.cpp
    git/Stashes.cpp
    git/Worktrees.cpp
    history/CommitDelegate.cpp
    history/CommitGraph.cpp
    history/CommitQuery.cpp
    history/HistorySearchField.cpp
    history/HistoryView.cpp
    history/HistoryViewCommit.cpp
    history/HistoryViewPanes.cpp
    index/OccurrenceGrammars.cpp
    index/OccurrenceIndex.cpp
    index/OccurrenceScanner.cpp
    index/OccurrenceStore.cpp
    index/PostingCodec.cpp
    index/VarintReader.cpp
    lsp/LanguageServers.cpp
    lsp/LspClient.cpp
    lsp/LspCompletionProvider.cpp
    lsp/LspProtocol.cpp
    lsp/ServerTasksLabel.cpp
    navigation/CodeIntelligence.cpp
    navigation/CodeIntelligenceCodeActions.cpp
    navigation/CodeIntelligenceHierarchy.cpp
    navigation/CodeIntelligenceMoveClass.cpp
    navigation/CodeIntelligenceRefactoring.cpp
    navigation/CodeIntelligenceSignature.cpp
    navigation/CodeIntelligenceUsages.cpp
    navigation/CodeNavigator.cpp
    navigation/Definitions.cpp
    navigation/GoToPopup.cpp
    navigation/HierarchyView.cpp
    navigation/NavigationHistory.cpp
    navigation/RecentFiles.cpp
    navigation/SymbolIndex.cpp
    navigation/UsageFinder.cpp
    navigation/UsagesView.cpp
    navigation/WorkspaceEditing.cpp
    refactor/EditJournal.cpp
    refactor/EditPreview.cpp
    refactor/EditSimulation.cpp
    refactor/FileReferences.cpp
    refactor/ChangeSignature.cpp
    refactor/ChangeSignaturePopup.cpp
    refactor/MoveClass.cpp
    refactor/ParsedText.cpp
    refactor/RefactoringPreviewPopup.cpp
    refactor/RenameAnalysis.cpp
    refactor/RenameSymbolPopup.cpp
    refactor/RefactoringHistory.cpp
    refactor/WorkspaceFiles.cpp
    outline/OutlineProviders.cpp
    outline/OutlineSymbol.cpp
    outline/OutlineView.cpp
    outline/SymbolIcons.cpp
    outline/TextOutlines.cpp
    outline/TreeSitterOutline.cpp
    projects/ProjectConfig.cpp
    projects/ProjectDetector.cpp
    projects/ProjectStore.cpp
    recipes/DeveloperEnvironment.cpp
    recipes/ProjectCreation.cpp
    recipes/Recipe.cpp
    recipes/RecipeLibrary.cpp
    recipes/RecipeSetup.cpp
    run/BuildOutputParser.cpp
    run/RunConfiguration.cpp
    run/RunConfigurationMenu.cpp
    run/Runner.cpp
    search/FindInFiles.cpp
    search/FindInFilesPopup.cpp
    settings/FontSettings.cpp
    settings/Keymap.cpp
    settings/Preferences.cpp
    settings/PreferencesPopup.cpp
    settings/TerminalPreferencesPage.cpp
    settings/TreeZoom.cpp
    setup/Prerequisites.cpp
    setup/PrerequisitesPopup.cpp
    syntax/HighlighterFactory.cpp
    syntax/HighlightQuery.cpp
    syntax/LanguageRegistry.cpp
    syntax/LexillaHighlighter.cpp
    syntax/PlainTextHighlighter.cpp
    syntax/ScintillaStyles.cpp
    syntax/TreeSitterFolds.cpp
    syntax/TreeSitterGrammars.cpp
    syntax/TreeSitterHighlighter.cpp
    terminal/IPty.h
    terminal/ProcessSpec.cpp
    terminal/TerminalWidget.cpp
    terminal/VtScreen.cpp
    toolchains/Installer.cpp
    toolchains/InstallPlan.cpp
    toolchains/JavaRuntimes.cpp
    toolchains/QtDownload.cpp
    toolchains/QtRepository.cpp
    toolchains/ToolCatalog.cpp
    toolchains/Toolchains.cpp
    ui/AppFonts.cpp
    ui/BrandMark.cpp
    ui/ClickableCursors.cpp
    ui/ContextMenus.cpp
    ui/DetachablePage.cpp
    ui/ElidingToolButton.cpp
    ui/FileIcons.cpp
    ui/FoldedPane.cpp
    ui/GripSplitter.cpp
    ui/HoverCloseButtons.cpp
    ui/HoverProgress.cpp
    ui/KdeThemes.cpp
    ui/LetterBadges.cpp
    ui/MessagePopup.cpp
    ui/Motion.cpp
    ui/NoticeBar.cpp
    ui/OverlayPopup.cpp
    ui/TabBarBaseLine.cpp
    ui/Paragraph.cpp
    ui/PopupWidgets.cpp
    ui/PopupMotion.cpp
    ui/Theme.cpp
    ui/ThemeStyle.cpp
    ui/TitleBar.cpp
    ui/TrailingLayout.cpp
    ui/TrailingTabButton.cpp
    ui/TreeColumnFit.cpp
    ui/TreeMetrics.cpp
    ui/UiIcons.cpp
    ui/UiMetrics.cpp
    ui/ViewHeader.cpp
    ui/VsCodeThemes.cpp
    ui/WindowControls.cpp
    ui/WindowFrame.cpp
    ui/WindowTitleBar.cpp
    ui/WorkspaceStatusBar.cpp
    viewers/DiagramKinds.cpp
    viewers/ImageView.cpp
    welcome/LanguageChoices.cpp
    welcome/NewProjectForm.cpp
    welcome/NewProjectPopup.cpp
    welcome/ProjectSettingsPopup.cpp
    welcome/ProjectSetupForm.cpp
    welcome/ProjectSetupPanel.cpp
    welcome/ProvisioningView.cpp
    welcome/WelcomeWindow.cpp
    workspace/CommandPalette.cpp
    workspace/ChoicePopup.cpp
    workspace/EntryPopups.cpp
    workspace/FileOperations.cpp
    workspace/FilesPanel.cpp
    workspace/FileTreeView.cpp
    workspace/FileUndoStack.cpp
    workspace/LastCommits.cpp
    workspace/ProjectFiles.cpp
    workspace/ProjectTreeModel.cpp
    workspace/SidePanel.cpp
    workspace/TreeDecorations.cpp
)

# The PTY depends on the system: ConPTY on Windows, forkpty elsewhere.
if(WIN32)
    target_sources(duet_core PRIVATE terminal/ConPty.cpp)
    # Public: ConPty.h includes <windows.h>, and so does every source that
    # includes it. WIN32_LEAN_AND_MEAN leaves out rpcndr.h, whose `small`
    # macro breaks a field of vterm.h.
    target_compile_definitions(duet_core PUBLIC _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
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
        ${PROJECT_SOURCE_DIR}/third_party/github-markdown-css/github-markdown-light.css
        ${PROJECT_SOURCE_DIR}/third_party/octicons/info-16.svg
        ${PROJECT_SOURCE_DIR}/third_party/octicons/light-bulb-16.svg
        ${PROJECT_SOURCE_DIR}/third_party/octicons/report-16.svg
        ${PROJECT_SOURCE_DIR}/third_party/octicons/alert-16.svg
        ${PROJECT_SOURCE_DIR}/third_party/octicons/stop-16.svg
        ${PROJECT_SOURCE_DIR}/third_party/octicons/link-16.svg)

# Schema of Claude Code's settings files, to check them in the editor.
qt_add_resources(duet_core schemas
    PREFIX /schemas
    BASE ${PROJECT_SOURCE_DIR}/third_party/schemastore
    FILES
        ${PROJECT_SOURCE_DIR}/third_party/schemastore/claude-code-settings.json)

# KDE's editor themes, those of Kate and KDevelop (see KdeThemes).
file(GLOB kde_themes CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/third_party/ksyntaxhighlighting-themes/*.theme)
qt_add_resources(duet_core kde_themes
    PREFIX /themes/kde
    BASE ${PROJECT_SOURCE_DIR}/third_party/ksyntaxhighlighting-themes
    FILES ${kde_themes})

# Interface and code fonts, the same on every system: Ubuntu Sans and
# Ubuntu Sans Mono by default; JetBrains Mono, the other code font, comes
# without ligatures (NL) and with them (the editor's option).
qt_add_resources(duet_core fonts
    PREFIX /fonts
    BASE ${PROJECT_SOURCE_DIR}/third_party/fonts
    FILES
        ${PROJECT_SOURCE_DIR}/third_party/fonts/UbuntuSans-Variable.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/UbuntuSans-VariableItalic.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/UbuntuSansMono-Variable.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/UbuntuSansMono-VariableItalic.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-Regular.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-Italic.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-Light.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-LightItalic.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-Medium.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-MediumItalic.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-Bold.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMonoNL-BoldItalic.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMono-Regular.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMono-Italic.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMono-Light.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMono-LightItalic.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMono-Medium.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMono-MediumItalic.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMono-Bold.ttf
        ${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMono-BoldItalic.ttf)

# The diagram editor: diagram families ported from sidecar, laid out by
# Graphviz. Without Graphviz, diagram files open as text.
if(GRAPHVIZ_FOUND)
    file(GLOB_RECURSE diagram_sources CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/viewers/diagram/*.cpp)
    target_sources(duet_core PRIVATE
        agent/mcp/DiagramTools.cpp
        viewers/DiagramCheck.cpp
        viewers/DiagramGenerationView.cpp
        viewers/DiagramView.cpp
        ${diagram_sources})
    target_link_libraries(duet_core PUBLIC PkgConfig::GRAPHVIZ)
    target_compile_definitions(duet_core PUBLIC DUET_WITH_DIAGRAMS)
    if(GVPLUGIN_core AND GVPLUGIN_dot_layout AND GVPLUGIN_neato_layout)
        target_link_libraries(duet_core PUBLIC ${GVPLUGIN_core} ${GVPLUGIN_dot_layout} ${GVPLUGIN_neato_layout})
        target_compile_definitions(duet_core PRIVATE DUET_GRAPHVIZ_PLUGINS)
    endif()
    if(TARGET Qt6::Svg)
        target_link_libraries(duet_core PUBLIC Qt6::Svg)
        target_compile_definitions(duet_core PRIVATE DUET_WITH_SVG)
    endif()
endif()

duet_enable_warnings(duet_core)

qt_add_executable(duet WIN32 MACOSX_BUNDLE
    app/main.cpp
    app/MainWindow.cpp
    app/MainWindowAgentProvider.cpp
    app/MainWindowAgents.cpp
    app/MainWindowClaude.cpp
    app/MainWindowEditing.cpp
    app/MainWindowGit.cpp
    app/MainWindowMenus.cpp
    app/MainWindowProject.cpp
    app/MainWindowRecipe.cpp
    app/MainWindowRun.cpp
    app/MainWindowSettings.cpp)
duet_enable_warnings(duet)
# Installed, it still finds the libraries linked from outside the system's
# paths (Graphviz's plugins, in a directory of their own).
set_target_properties(duet PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
# The executable's icon on Windows (Explorer, taskbar).
if(WIN32)
    target_sources(duet PRIVATE ${PROJECT_SOURCE_DIR}/packaging/windows/duet.rc)
endif()
target_link_libraries(duet PRIVATE duet_core)
target_compile_definitions(duet PRIVATE DUET_VERSION="${PROJECT_VERSION}")
set_target_properties(duet PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_dependencies(duet duet-hook duet-mcp duet-askpass)

# ClaudeLauncher looks for duet-hook and duet-mcp next to the executable, and
# AskpassServer for duet-askpass: on macOS,
# that is inside the bundle (Duet.app/Contents/MacOS), with Duet's Claude
# plugin and recipes in Contents/Resources.
if(APPLE)
    set_target_properties(duet PROPERTIES OUTPUT_NAME Duet)
    add_custom_command(TARGET duet POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:duet-hook> $<TARGET_FILE_DIR:duet>
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:duet-mcp> $<TARGET_FILE_DIR:duet>
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:duet-askpass> $<TARGET_FILE_DIR:duet>
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/bin/claude-plugin
                $<TARGET_FILE_DIR:duet>/../Resources/claude-plugin
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/bin/recipes
                $<TARGET_FILE_DIR:duet>/../Resources/recipes)
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
