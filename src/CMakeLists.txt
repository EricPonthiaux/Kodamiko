# The log files, shared by the editor and its helper programs (tools/).
add_library(kdk_log STATIC log/LogFiles.cpp)
target_include_directories(kdk_log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kdk_log PUBLIC Qt6::Core)
target_compile_definitions(kdk_log PRIVATE KDK_VERSION="${PROJECT_VERSION}")
kdk_enable_warnings(kdk_log)

# The system's ways (shells, executables, file names...), shared by the
# editor and its helper programs: an interface per concept, and one source
# per system implementing it.
add_library(kdk_system STATIC system/Executables.cpp system/Host.cpp)
if(WIN32)
    target_sources(kdk_system PRIVATE
        system/windows/ExecutablesWindows.cpp
        system/windows/FilePathsWindows.cpp
        system/windows/HostWindows.cpp
        system/windows/ShellWindows.cpp
        system/windows/StandardStreamsWindows.cpp)
else()
    target_sources(kdk_system PRIVATE
        system/unix/ExecutablesUnix.cpp
        system/unix/FilePathsUnix.cpp
        system/unix/ShellUnix.cpp
        system/unix/StandardStreamsUnix.cpp)
    if(APPLE)
        target_sources(kdk_system PRIVATE system/osx/HostMacOS.cpp)
    else()
        target_sources(kdk_system PRIVATE system/linux/HostLinux.cpp)
    endif()
endif()
target_include_directories(kdk_system PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kdk_system PUBLIC Qt6::Core)
kdk_enable_warnings(kdk_system)

# Library containing the whole editor except main(): the executable and the
# tests link against it.
add_library(kdk_core STATIC
    agent/AgentActivity.cpp
    agent/AgentCheckPopup.cpp
    agent/AgentIndicator.cpp
    agent/AgentMergeQueue.cpp
    agent/AgentProviders.cpp
    agent/AgentRun.cpp
    agent/AgentSessions.cpp
    agent/CloseAgentPopup.cpp
    agent/CommitMessage.cpp
    agent/DiagramRequest.cpp
    agent/DiagramRequestPopup.cpp
    agent/EditErrors.cpp
    agent/EditorContext.cpp
    agent/HookServer.cpp
    agent/PermissionPrompts.cpp
    agent/mcp/CodeTools.cpp
    agent/mcp/EditorTools.cpp
    agent/mcp/TabTools.cpp
    agent/mcp/GitBranchTools.cpp
    agent/mcp/GitIntegrationTools.cpp
    agent/mcp/GitToolSupport.cpp
    agent/mcp/GitTools.cpp
    agent/mcp/McpRegistry.cpp
    agent/mcp/McpSession.cpp
    agent/mcp/RefactoringTools.cpp
    agent/mcp/TerminalTools.cpp
    agent/mcp/WorkspaceTools.cpp
    agent/NewAgentPopup.cpp
    agent/ProjectCheck.cpp
    agent/ReviewBar.cpp
    agent/ReviewExcerpt.cpp
    agent/ReviewFileHeader.cpp
    agent/ReviewFindingCard.cpp
    agent/ReviewFindingsList.cpp
    agent/ReviewFindingsPanel.cpp
    agent/ReviewFindingsView.cpp
    agent/ReviewPage.cpp
    agent/ReviewProgressView.cpp
    agent/ReviewReport.cpp
    agent/ReviewRequest.cpp
    agent/ReviewSession.cpp
    agent/ReviewSketch.cpp
    agent/ReviewText.cpp
    agent/ReviewView.cpp
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
    agent/vendor/claude/GitCommandGuard.cpp
    agent/vendor/claude/GitCommandTranslations.cpp
    agent/vendor/claude/ShellCommandLine.cpp
    agent/vendor/claude/UnsavedEditGuard.cpp
    changes/BranchesPopup.cpp
    changes/ChangeMap.cpp
    changes/ChangeTree.cpp
    changes/ChangesView.cpp
    changes/ChangesViewClaude.cpp
    changes/ChangesViewConflicts.cpp
    changes/ChangesViewDiff.cpp
    changes/ChangesViewList.cpp
    changes/ChangesViewReview.cpp
    changes/CleanDiffState.cpp
    changes/CommitPanel.cpp
    changes/DiffFileStrip.cpp
    changes/DiffHeaderBar.cpp
    changes/DiffLayout.cpp
    changes/SideBySide.cpp
    changes/SideBySideDivider.cpp
    changes/DiffStyles.cpp
    changes/DiffSyntaxHighlighter.cpp
    changes/DiffView.cpp
    changes/DiffViewNotes.cpp
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
    editor/ConflictActionBar.cpp
    editor/EditorStatus.cpp
    editor/EditorTabs.cpp
    editor/EmptyEditorBackdrop.cpp
    editor/EditorWidget.cpp
    editor/FileWatcher.cpp
    editor/FindBar.cpp
    editor/MarkdownHtml.cpp
    editor/MarkdownToolTip.cpp
    editor/MergeConflicts.cpp
    editor/PreviewPage.cpp
    editor/PreviewSite.cpp
    editor/PreviewTab.cpp
    editor/ScrollBarMarks.cpp
    editor/SymbolOccurrences.cpp
    editor/TextFile.cpp
    git/Askpass.cpp
    git/Branches.cpp
    git/CommitLog.cpp
    git/FileBlame.cpp
    git/ConflictMarkers.cpp
    git/ConflictVersions.cpp
    git/ContentMatcher.cpp
    git/CredentialCache.cpp
    git/FilePatch.cpp
    git/FileStatuses.cpp
    git/GitCommands.cpp
    git/GitIgnoreFile.cpp
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
    git/ThreeWayMerge.cpp
    git/Worktrees.cpp
    history/CommitDelegate.cpp
    history/CommitGraph.cpp
    history/CommitQuery.cpp
    history/CommitTrack.cpp
    history/FileLens.cpp
    history/HistorySearchField.cpp
    history/HistoryView.cpp
    history/HistoryViewCommit.cpp
    history/HistoryViewLens.cpp
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
    merge/MergeChoicePanel.cpp
    merge/MergeDocument.cpp
    merge/MergeGutter.cpp
    merge/MergePane.cpp
    merge/MergeTextPanel.cpp
    merge/MergeTexts.cpp
    merge/MergeView.cpp
    merge/SimpleConflicts.cpp
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
    projects/ProjectWindowServer.cpp
    recipes/DeveloperEnvironment.cpp
    recipes/ProjectCreation.cpp
    recipes/Recipe.cpp
    recipes/RecipeLibrary.cpp
    recipes/RecipeSchema.cpp
    recipes/RecipeServers.cpp
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
    settings/PreferencesPopupUi.cpp
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
    terminal/KeyModifiers.h
    terminal/OptionKey.h
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
    ui/AboutPopup.cpp
    ui/AppFonts.cpp
    ui/BrandHero.cpp
    ui/BrandLogo.cpp
    ui/BrandMark.cpp
    ui/BusyIndicator.cpp
    ui/ClickableCursors.cpp
    ui/ContextMenus.cpp
    ui/DetachablePage.cpp
    ui/ElidingToolButton.cpp
    ui/FileIcons.cpp
    ui/FoldedPane.cpp
    ui/GripSplitter.cpp
    ui/HoverCloseButtons.cpp
    ui/HoverProgress.cpp
    ui/LetterBadges.cpp
    ui/MaterialThemes.cpp
    ui/MessagePopup.cpp
    ui/ModeTabBar.cpp
    ui/Motion.cpp
    ui/NoticeBar.cpp
    ui/Snackbar.cpp
    ui/SnackbarButton.cpp
    ui/OverlayPopup.cpp
    ui/TabBarBaseLine.cpp
    ui/Paragraph.cpp
    ui/PopupFooter.cpp
    ui/PopupRows.cpp
    ui/PopupSearchField.cpp
    ui/SearchPopupWidgets.cpp
    ui/PopupWidgets.cpp
    ui/SettingsSwitch.cpp
    ui/PopupMotion.cpp
    ui/RotatingQuote.cpp
    ui/Spinner.cpp
    ui/StepTimeline.cpp
    ui/StrayWindowCheck.cpp
    ui/Theme.cpp
    ui/ThemeStyle.cpp
    ui/ToolTipPlacement.cpp
    ui/TitleBar.cpp
    ui/TrailingLayout.cpp
    ui/TrailingTabButton.cpp
    ui/TreeColumnFit.cpp
    ui/TreeMetrics.cpp
    ui/UiIcons.cpp
    ui/UiMetrics.cpp
    ui/ViewHeader.cpp
    ui/WindowControls.cpp
    ui/WindowFrame.cpp
    ui/WindowTitleBar.cpp
    ui/WorkspaceStatusBar.cpp
    viewers/DiagramKinds.cpp
    viewers/ImageView.cpp
    welcome/LanguageChoices.cpp
    welcome/NewProjectForm.cpp
    welcome/NewProjectFormUi.cpp
    welcome/NewProjectPopup.cpp
    welcome/ProjectAvatar.cpp
    welcome/ProjectListDelegate.cpp
    welcome/ProjectSettingsPopup.cpp
    welcome/ProjectSetupForm.cpp
    welcome/ProjectSetupFormUi.cpp
    welcome/ProjectSetupPanel.cpp
    welcome/ProvisioningView.cpp
    welcome/ResumeCard.cpp
    welcome/WelcomeWindow.cpp
    welcome/WelcomeWindowUi.cpp
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

# The features that work differently on each system: one source per
# system. The PTY is ConPTY on Windows, forkpty elsewhere.
if(WIN32)
    target_sources(kdk_core PRIVATE
        editor/windows/WebZoomWindows.cpp
        git/windows/CredentialCacheWindows.cpp
        projects/windows/NewInstanceWindows.cpp
        projects/windows/WindowForegroundWindows.cpp
        recipes/windows/VisualStudio.cpp
        setup/windows/SystemSetupWindows.cpp
        terminal/windows/ConPty.cpp
        terminal/windows/KeyModifiersWindows.cpp
        toolchains/windows/SystemJdksWindows.cpp
        ui/windows/DesktopIntegrationWindows.cpp)
    # Public: ConPty.h includes <windows.h>, and so does every source that
    # includes it. WIN32_LEAN_AND_MEAN leaves out rpcndr.h, whose `small`
    # macro breaks a field of vterm.h.
    target_compile_definitions(kdk_core PUBLIC _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_sources(kdk_core PRIVATE
        git/unix/CredentialCacheUnix.cpp
        projects/unix/WindowForegroundUnix.cpp
        terminal/unix/UnixPty.cpp)
    if(APPLE)
        target_sources(kdk_core PRIVATE
            editor/osx/WebZoomMacOS.cpp
            projects/osx/NewInstanceMacOS.cpp
            setup/osx/SystemSetupMacOS.cpp
            terminal/osx/KeyModifiersMacOS.cpp
            terminal/osx/PtyForegroundMacOS.cpp
            toolchains/osx/SystemJdksMacOS.cpp
            ui/osx/DesktopIntegrationMacOS.mm)
        target_link_libraries(kdk_core PRIVATE "-framework AppKit")
    else()
        target_sources(kdk_core PRIVATE
            editor/linux/WebZoomLinux.cpp
            projects/linux/NewInstanceLinux.cpp
            setup/linux/SystemSetupLinux.cpp
            terminal/linux/KeyModifiersLinux.cpp
            terminal/linux/PtyForegroundLinux.cpp
            toolchains/linux/SystemJdksLinux.cpp
            ui/linux/DesktopIntegrationLinux.cpp)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(kdk_core PRIVATE util) # forkpty() before glibc 2.34
    endif()
endif()

target_include_directories(kdk_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kdk_core PUBLIC
    kdk_system Qt6::Widgets Qt6::Network Qt6::WebEngineWidgets
    qscintilla tree_sitter_grammars vterm git2 cmark_gfm)
kdk_add_queries(kdk_core)

# Original language and framework logos for the project templates.
# PNG exports keep these available even when Qt's SVG plugin is not installed.
qt_add_resources(kdk_core template_icons
    PREFIX /template-icons
    BASE ${PROJECT_SOURCE_DIR}/third_party/devicon
    FILES
        ${PROJECT_SOURCE_DIR}/third_party/devicon/angular.png
        ${PROJECT_SOURCE_DIR}/third_party/devicon/cpp.png
        ${PROJECT_SOURCE_DIR}/third_party/devicon/java.png
        ${PROJECT_SOURCE_DIR}/third_party/devicon/python.png
        ${PROJECT_SOURCE_DIR}/third_party/devicon/qt.png
        ${PROJECT_SOURCE_DIR}/third_party/devicon/typescript.png
        ${PROJECT_SOURCE_DIR}/third_party/devicon/LICENSE)

# GitHub's Markdown style sheets and icons, for the Markdown preview.
qt_add_resources(kdk_core markdown_assets
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
qt_add_resources(kdk_core schemas
    PREFIX /schemas
    BASE ${PROJECT_SOURCE_DIR}/third_party/schemastore
    FILES
        ${PROJECT_SOURCE_DIR}/third_party/schemastore/claude-code-settings.json)

# Schemas of Kodamiko's own files, to check the recipes as they are read.
qt_add_resources(kdk_core kdk_schemas
    PREFIX /schemas
    BASE ${PROJECT_SOURCE_DIR}/tools/schemas
    FILES
        ${PROJECT_SOURCE_DIR}/tools/schemas/recipe.schema.json
        ${PROJECT_SOURCE_DIR}/tools/schemas/run.schema.json)

# Interface and code fonts, the same on every system: Ubuntu Sans and
# Droid Sans Mono by default; Ubuntu Sans Mono is also offered. JetBrains
# Mono comes without ligatures (NL) and with them (the editor's option).
qt_add_resources(kdk_core fonts
    PREFIX /fonts
    BASE ${PROJECT_SOURCE_DIR}/third_party/fonts
    FILES
        ${PROJECT_SOURCE_DIR}/third_party/fonts/DroidSansMono.ttf
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
    target_sources(kdk_core PRIVATE
        agent/mcp/DiagramTools.cpp
        viewers/DiagramCheck.cpp
        viewers/DiagramGenerationView.cpp
        viewers/sketch/DiagramSketch.cpp
        viewers/sketch/SketchPainting.cpp
        viewers/sketch/SketchScenes.cpp
        viewers/DiagramView.cpp
        ${diagram_sources})
    target_link_libraries(kdk_core PUBLIC ${GRAPHVIZ_LIBRARIES})
    target_compile_definitions(kdk_core PUBLIC KDK_WITH_DIAGRAMS)
    if(GVPLUGIN_core AND GVPLUGIN_dot_layout AND GVPLUGIN_neato_layout)
        target_link_libraries(kdk_core PUBLIC ${GVPLUGIN_core} ${GVPLUGIN_dot_layout} ${GVPLUGIN_neato_layout})
        target_compile_definitions(kdk_core PRIVATE KDK_GRAPHVIZ_PLUGINS)
    endif()
    if(TARGET Qt6::Svg)
        target_link_libraries(kdk_core PUBLIC Qt6::Svg)
        target_compile_definitions(kdk_core PRIVATE KDK_WITH_SVG)
    endif()
endif()

kdk_enable_warnings(kdk_core)

qt_add_executable(kodamiko WIN32 MACOSX_BUNDLE
    app/main.cpp
    app/FileOpenRouter.cpp
    app/MainWindow.cpp
    app/MainWindowAgentProvider.cpp
    app/MainWindowAgents.cpp
    app/MainWindowClaude.cpp
    app/MainWindowEditing.cpp
    app/MainWindowGit.cpp
    app/MainWindowMenus.cpp
    app/MainWindowProject.cpp
    app/MainWindowRecipe.cpp
    app/MainWindowReview.cpp
    app/MainWindowRun.cpp
    app/MainWindowSettings.cpp)
if(WIN32)
    target_sources(kodamiko PRIVATE app/windows/MainWindowTitleBarWindows.cpp app/windows/NativeNotificationsWindows.cpp)
elseif(APPLE)
    target_sources(kodamiko PRIVATE app/osx/MainWindowTitleBarMacOS.mm app/osx/NativeNotificationsMacOS.mm)
    target_link_libraries(kodamiko PRIVATE "-framework AppKit" "-framework UserNotifications")
else()
    target_sources(kodamiko PRIVATE app/linux/MainWindowTitleBarLinux.cpp app/linux/NativeNotificationsLinux.cpp)
endif()
kdk_enable_warnings(kodamiko)
kdk_relink_while_running(kodamiko)
# Installed, it still finds the libraries linked from outside the system's
# paths (Graphviz's plugins, in a directory of their own).
set_target_properties(kodamiko PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
# The executable's icon and version information on Windows (Explorer,
# taskbar, Task Manager), and its manifest.
if(WIN32)
    configure_file(${PROJECT_SOURCE_DIR}/packaging/windows/kodamiko.rc.in ${CMAKE_CURRENT_BINARY_DIR}/kodamiko.rc @ONLY)
    target_sources(kodamiko PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/kodamiko.rc
                                ${PROJECT_SOURCE_DIR}/packaging/windows/kodamiko.manifest)
endif()
target_link_libraries(kodamiko PRIVATE kdk_core kdk_log)
target_compile_definitions(kodamiko PRIVATE KDK_VERSION="${PROJECT_VERSION}")
set_target_properties(kodamiko PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_dependencies(kodamiko kodamiko-hook kodamiko-mcp kodamiko-askpass)

# ClaudeLauncher looks for kodamiko-hook and kodamiko-mcp next to the executable, and
# AskpassServer for kodamiko-askpass: on macOS,
# that is inside the bundle (Kodamiko.app/Contents/MacOS), with Kodamiko's Claude
# plugin and recipes in Contents/Resources.
if(APPLE)
    set_target_properties(kodamiko PROPERTIES OUTPUT_NAME Kodamiko)
    # The bundle's identity: notifications, privacy permissions and Spaces
    # assignment are tied to its identifier.
    set(kdk_icon ${PROJECT_SOURCE_DIR}/packaging/macos/kodamiko.icns)
    set_source_files_properties(${kdk_icon} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(kodamiko PRIVATE ${kdk_icon})
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(KDK_MACOS_MINIMUM_VERSION ${CMAKE_OSX_DEPLOYMENT_TARGET})
    else()
        set(KDK_MACOS_MINIMUM_VERSION ${QT_SUPPORTED_MIN_MACOS_VERSION})
    endif()
    set_target_properties(kodamiko PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.ericponthiaux.kodamiko
        MACOSX_BUNDLE_BUNDLE_NAME Kodamiko
        MACOSX_BUNDLE_ICON_FILE kodamiko.icns
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT "Licensed under the GNU GPL version 3.")

    # macOS identifies an ad-hoc signed application by its code's hash: each
    # rebuild asks again for the protected folders (Desktop, Documents…),
    # for Kodamiko and the processes it starts. Signed with a stable identity
    # (a development certificate, or a self-signed one in the keychain), it
    # asks once. The helpers first, then the bundle that holds them.
    set(KDK_CODESIGN_IDENTITY "" CACHE STRING
        "Identity that signs Kodamiko.app on macOS, as codesign names it (empty: ad-hoc)")
    set(kdk_signing)
    if(KDK_CODESIGN_IDENTITY)
        set(kdk_codesign codesign --force --timestamp=none --sign ${KDK_CODESIGN_IDENTITY})
        set(kdk_signing
            COMMAND ${kdk_codesign} $<TARGET_FILE_DIR:kodamiko>/kodamiko-hook
            COMMAND ${kdk_codesign} $<TARGET_FILE_DIR:kodamiko>/kodamiko-mcp
            COMMAND ${kdk_codesign} $<TARGET_FILE_DIR:kodamiko>/kodamiko-askpass
            COMMAND ${kdk_codesign} $<TARGET_BUNDLE_DIR:kodamiko>)
    endif()

    # The helpers, the plugin and the recipes go into the bundle whenever
    # one of them changes, not only when kodamiko links: a step of its own, after
    # kodamiko. Signing comes last, as a change to a signed bundle breaks its
    # seal.
    file(GLOB_RECURSE kdk_bundle_resources CONFIGURE_DEPENDS
        ${PROJECT_SOURCE_DIR}/tools/claude-plugin/*
        ${PROJECT_SOURCE_DIR}/tools/recipes/*/*)
    set(kdk_bundle_stamp ${CMAKE_CURRENT_BINARY_DIR}/kodamiko-bundle.stamp)
    add_custom_command(OUTPUT ${kdk_bundle_stamp}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kodamiko-hook> $<TARGET_FILE_DIR:kodamiko>
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kodamiko-mcp> $<TARGET_FILE_DIR:kodamiko>
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kodamiko-askpass> $<TARGET_FILE_DIR:kodamiko>
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/bin/claude-plugin
                $<TARGET_FILE_DIR:kodamiko>/../Resources/claude-plugin
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/bin/recipes
                $<TARGET_FILE_DIR:kodamiko>/../Resources/recipes
        ${kdk_signing}
        COMMAND ${CMAKE_COMMAND} -E touch ${kdk_bundle_stamp}
        DEPENDS kodamiko kodamiko-hook kodamiko-mcp kodamiko-askpass ${kdk_bundle_resources}
        COMMENT "Copying the helpers, the plugin and the recipes into Kodamiko.app"
        VERBATIM)
    add_custom_target(kodamiko-bundle ALL DEPENDS ${kdk_bundle_stamp})
endif()

# Windows has no rpath: without Qt's DLLs next to it, the built executable
# does not start. windeployqt only copies what changed since the last build.
if(WIN32)
    add_custom_command(TARGET kodamiko POST_BUILD
        COMMAND $<TARGET_FILE:Qt6::windeployqt> --no-translations --no-compiler-runtime
                $<TARGET_FILE:kodamiko>
        COMMENT "Deploying Qt next to kodamiko.exe")
endif()

# "kodamiko" component: the dependencies (libgit2) declare their own
# install rules, which we do not want in the packages.
#   cmake --install build --component kodamiko --prefix <folder>
include(GNUInstallDirs)
install(TARGETS kodamiko
    BUNDLE DESTINATION . COMPONENT kodamiko
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT kodamiko)
# Windows has no rpath: Graphviz's DLLs go next to the executable.
if(GRAPHVIZ_RUNTIME_DLLS)
    install(FILES ${GRAPHVIZ_RUNTIME_DLLS}
        DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT kodamiko)
endif()
if(UNIX AND NOT APPLE)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/kodamiko.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT kodamiko)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/kodamiko.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT kodamiko)
endif()
