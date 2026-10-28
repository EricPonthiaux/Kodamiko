// SPDX-License-Identifier: GPL-3.0-only

#include "MainWindow.h"

#include "agent/AgentCheckPopup.h"
#include "agent/AgentIndicator.h"
#include "agent/AgentProviders.h"
#include "agent/DiagramRequestPopup.h"
#include "agent/EditErrors.h"
#include "agent/NewAgentPopup.h"
#include "agent/ReviewPage.h"
#include "agent/mcp/DuetTools.h"
#include "agent/vendor/claude/ClaudeConfigView.h"
#include "agent/vendor/claude/ClaudeLinter.h"
#include "changes/BranchesPopup.h"
#include "changes/ChangesView.h"
#include "changes/GitActions.h"
#include "changes/NewBranchPopup.h"
#include "changes/SnapshotsPopup.h"
#include "changes/StashesPopup.h"
#include "completion/CodeGeneration.h"
#include "completion/CompletionController.h"
#include "completion/CompletionService.h"
#include "completion/DocComments.h"
#include "completion/LocalCompletion.h"
#include "completion/NameSuggestions.h"
#include "completion/Templates.h"
#include "diagnostics/ProblemStore.h"
#include "diagnostics/ProblemsView.h"
#include "editor/EditorStatus.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/FindBar.h"
#include "git/GitCommands.h"
#include "history/HistoryView.h"
#include "index/OccurrenceIndex.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspCompletionProvider.h"
#include "lsp/ServerTasksLabel.h"
#include "navigation/CodeIntelligence.h"
#include "navigation/CodeNavigator.h"
#include "navigation/GoToPopup.h"
#include "navigation/HierarchyView.h"
#include "navigation/SymbolIndex.h"
#include "navigation/UsagesView.h"
#include "outline/OutlineView.h"
#include "projects/ProjectStore.h"
#include "run/RunConfiguration.h"
#include "run/Runner.h"
#include "search/FindInFilesPopup.h"
#include "settings/FontSettings.h"
#include "settings/Keymap.h"
#include "settings/Preferences.h"
#include "setup/Prerequisites.h"
#include "terminal/TerminalWidget.h"
#include "toolchains/Toolchains.h"
#include "ui/BusyIndicator.h"
#include "ui/DetachablePage.h"
#include "ui/ElidingToolButton.h"
#include "ui/GripSplitter.h"
#include "ui/HoverCloseButtons.h"
#include "ui/MessagePopup.h"
#include "ui/ModeTabBar.h"
#include "ui/NoticeBar.h"
#include "ui/ThemeStyle.h"
#include "ui/TitleBar.h"
#include "ui/TrailingTabButton.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"
#include "ui/WindowFrame.h"
#include "ui/WorkspaceStatusBar.h"
#include "welcome/ProjectSettingsPopup.h"
#include "workspace/CommandPalette.h"
#include "workspace/FileTreeView.h"
#include "workspace/FilesPanel.h"
#include "workspace/SidePanel.h"

#include <QActionGroup>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <utility>

namespace {

// Between the window's left edge and the agent indicator: macOS rounds the
// window's bottom corners, and the indicator must clear them.
QColor problemsIconColor(int errorCount, int warningCount, const Theme &theme)
{
    if (errorCount > 0)
        return theme.error;
    if (warningCount > 0)
        return theme.warning;
    return theme.mutedText;
}

QList<QAction *> menuCommands(const QWidget *menu)
{
    QList<QAction *> commands;
    for (QAction *action : menu->actions()) {
        if (const QMenu *submenu = action->menu())
            commands += menuCommands(submenu);
        else if (!action->isSeparator())
            commands.append(action);
    }
    return commands;
}

// The modes by the names QSettings keeps them under ("mode").
const std::array<std::pair<MainWindow::Mode, QLatin1StringView>, 5> modeNames{{
    {MainWindow::Mode::Terminal, QLatin1StringView("terminal")},
    {MainWindow::Mode::Edit, QLatin1StringView("edit")},
    {MainWindow::Mode::Review, QLatin1StringView("review")},
    {MainWindow::Mode::Diff, QLatin1StringView("diff")},
    {MainWindow::Mode::History, QLatin1StringView("history")},
}};

// The mode the window was closed in: by its name, or by the number the
// versions before the Review mode kept ("viewMode"); Edit by default.
MainWindow::Mode savedMode(const QSettings &settings)
{
    using Mode = MainWindow::Mode;
    const QString name = settings.value(QStringLiteral("mode")).toString();
    for (const auto &[mode, modeName] : modeNames) {
        if (name == modeName)
            return mode;
    }
    constexpr std::array<Mode, 4> modesBeforeReview{Mode::Terminal, Mode::Edit, Mode::Diff, Mode::History};
    const int number = settings.value(QStringLiteral("viewMode"), 1).toInt();
    return modesBeforeReview.at(std::clamp(number, 0, int(modesBeforeReview.size()) - 1));
}

QString nameOf(MainWindow::Mode mode)
{
    for (const auto &[each, name] : modeNames) {
        if (each == mode)
            return name;
    }
    return {};
}

} // namespace

MainWindow::MainWindow(const QString &folder, QWidget *parent)
    : MainWindow(folder, Preferences::load(), parent)
{
}

MainWindow::MainWindow(const QString &folder, const Preferences &preferences, QWidget *parent)
    : QMainWindow(parent)
    , theme_(Theme::named(preferences.theme))
    , agentProvider_(&AgentProviders::find(preferences.defaultAgentProvider))
{
    preferences.applyTypography(theme_);
    ThemeStyle::apply(theme_);
    TerminalWidget::setOptionKey(preferences.terminalOptionKey);
    createWidgets(preferences);
    createMenus();
    applyKeymap(Keymap::loadOverrides());

    terminalWindow_->setWindowActions(menuCommands(titleBar_->menuBar()));
    restoreSettings();
    connect(&hookServer_, &HookServer::hookReceived, this, &MainWindow::onHookReceived);
    connect(&projectWindowServer_, &ProjectWindowServer::raiseRequested, this, &MainWindow::bringToFront);
    registerMcpTools();
    connect(&agentActivity_, &AgentActivity::stateChanged, this, &MainWindow::updateAgentIndicator);
    connect(&agentActivity_, &AgentActivity::stateChanged, this, &MainWindow::keepAwakeWhileAgentWorks);
    connect(&agentActivity_, &AgentActivity::attentionRequested, this, &MainWindow::notify);
    connect(&agentSessions_, &AgentSessions::activityChanged, this, &MainWindow::updateAgentTab);
    connect(&agentSessions_, &AgentSessions::attentionRequested, this, &MainWindow::notify);

    connect(&agentSessions_, &AgentSessions::sessionForgotten, this, &MainWindow::closeWindowOnceAgentsClosed,
            Qt::QueuedConnection);
    if (!hookServer_.listen())
        statusBar()->showMessage(
            tr("%1 tracking unavailable: %2").arg(agentProvider_->name(), hookServer_.errorString()));
    openFolder(ProjectStore::startFolder(folder));
    setReviewStep(isReviewStepOn());
    setMode(savedMode(QSettings()));

    if (!Prerequisites::allFound())
        QTimer::singleShot(0, this, &MainWindow::showPrerequisites);
}

MainWindow::~MainWindow()
{
    // While the window is whole: stopping clears the diagnostics, whose
    // signals reach the window's slots; the servers' own destructor would
    // run after the window's.
    languageServers_->stopAll();
    delete projectSettings_;
}

void MainWindow::createWidgets(const Preferences &preferences)
{
    titleBar_ = new TitleBar(this);
    setMenuWidget(titleBar_);
    frame_ = new WindowFrame(this);
    projectMenu_ = new QMenu(this);
    projectMenu_->setObjectName(QStringLiteral("projectMenu"));

    editorTabs_ = new EditorTabs(theme_, this);
    editorTabs_->setEditorOptions(preferences.editor);
    connect(editorTabs_, &EditorTabs::statusMessage, this, &MainWindow::showStatus);

    createCodeServices(preferences);
    createGitServices();
    createGitViews();
    createCentralWidget(preferences);
}

void MainWindow::createCodeServices(const Preferences &preferences)
{
    symbolIndex_ = new SymbolIndex(this);
    occurrenceIndex_ = new OccurrenceIndex(this);
    connect(editorTabs_, &EditorTabs::fileSaved, this, &MainWindow::refreshIndexes);

    toolchains_ = std::make_unique<Toolchains>();
    languageServers_ = new LanguageServers(this);
    languageServers_->setEnabled(preferences.languageServers);
    connect(languageServers_, &LanguageServers::statusMessage, this, &MainWindow::showStatus);
    editErrorCheck_ = new EditErrorCheck(languageServers_, this);

    problemStore_ = new ProblemStore(languageServers_, this);
    runner_ = new Runner(problemStore_, this);
    connect(runner_, &Runner::selectionChanged, this, &MainWindow::updateRunWidgets);
    connect(runner_, &Runner::started, this, [this](const RunConfiguration &configuration) {
        statusBar()->showMessage(configuration.kind == RunConfiguration::Kind::Build
                                     ? tr("Building \u201c%1\u201d…").arg(configuration.name)
                                     : tr("Running \u201c%1\u201d…").arg(configuration.name));
        updateRunWidgets();
    });
    connect(runner_, &Runner::finished, this, &MainWindow::onRunFinished);
    completion_ = new CompletionService(this);
    completion_->addProvider(std::make_shared<LspCompletionProvider>(languageServers_));
    completion_->addProvider(std::make_shared<ProjectSymbolsProvider>(symbolIndex_));
    completion_->addProvider(std::make_shared<KeywordProvider>());
    completion_->addProvider(std::make_shared<DocumentWordsProvider>());
    completion_->addProvider(std::make_shared<PostfixTemplateProvider>());
    completion_->addProvider(std::make_shared<LiveTemplateProvider>());
    completion_->addProvider(std::make_shared<NameSuggestionProvider>());
    completion_->addProvider(std::make_shared<CodeGenerationProvider>(symbolIndex_, &CodeIntelligence::counterpartOf));
    completion_->addProvider(std::make_shared<DocCommentProvider>());
    fullLineModel_ = new LocalFullLineModel(this);
    connect(fullLineModel_, &LocalFullLineModel::failed, this, &MainWindow::showStatus);
    applyFullLine(preferences);
    connect(editorTabs_, &EditorTabs::editorOpened, this, [this](EditorWidget *editor) {
        editor->setFileActions(editorContextActions_);

        const auto *controller = new CompletionController(editor, completion_);

        connect(controller, &CompletionController::callInserted, this, [this, editor] {
            if (intelligence_)
                intelligence_->requestParameterInfo(editor);
        });

        QTimer::singleShot(0, editor, [this, editor] { warmUpLanguageServer(editor); });
    });
    connect(editorTabs_, &EditorTabs::fileSaved, this, [this] { fileStatuses_->refresh(); });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {
            refreshIndexes();
            fileStatuses_->refresh();
        }
    });
    navigator_ = new CodeNavigator(editorTabs_, symbolIndex_, this);

    intelligence_ = new CodeIntelligence(
        {editorTabs_, languageServers_, navigator_, problemStore_, &occurrenceIndex_->store()}, this);
    connect(editorTabs_, &EditorTabs::editorOpened, intelligence_, &CodeIntelligence::attach);
    connect(editorTabs_, &EditorTabs::definitionRequested, intelligence_, &CodeIntelligence::goToDefinition);
    connect(intelligence_, &CodeIntelligence::statusMessage, this, &MainWindow::showStatus);
    connect(navigator_, &CodeNavigator::statusMessage, this, &MainWindow::showStatus);
}

void MainWindow::createGitViews()
{
    changesView_ = new ChangesView(theme_, gitCommands_, this);
    connect(changesView_, &ChangesView::openFileRequested, this, [this](const QString &relativePath, int line) {
        setMode(Mode::Edit);
        navigator_->goToLine(absolutePath(relativePath), line);
    });
    connect(changesView_, &ChangesView::statusMessage, this, &MainWindow::showStatus);
    connect(changesView_, &ChangesView::commitMessageRequested, this, &MainWindow::askClaudeForCommitMessage);
    connect(changesView_, &ChangesView::explanationRequested, this,
            [this](const QString &request) { typeInClaudePrompt(request, PromptEntry::Submitted); });
    connect(changesView_, &ChangesView::reviewRequested, this, &MainWindow::reviewStagedChanges);
    connect(changesView_, &ChangesView::reviewCloseRequested, this, &MainWindow::closeStagedReview);
    connect(changesView_, &ChangesView::continueRequested, gitActions_, &GitActions::continueOperation);
    connect(changesView_, &ChangesView::skipRequested, gitActions_, &GitActions::skipCommit);
    connect(changesView_, &ChangesView::abortRequested, gitActions_, &GitActions::abortOperation);

    historyView_ = new HistoryView(theme_, this);
    connect(historyView_, &HistoryView::checkoutRequested, gitActions_, &GitActions::checkoutCommit);
    connect(historyView_, &HistoryView::openFileRequested, this, [this](const QString &relativePath) {
        setMode(Mode::Edit);
        editorTabs_->openFile(absolutePath(relativePath));
    });
}

void MainWindow::createCentralWidget(const Preferences &preferences)
{
    modes_ = new QStackedWidget(this);
    modes_->addWidget(createTerminalPage());
    modes_->addWidget(createEditPage());
    modes_->addWidget(createReviewPage());
    modes_->addWidget(changesView_);
    modes_->addWidget(historyView_);

    terminalWindow_ = new DetachablePage(
        modes_, terminalPage_, QStringLiteral("terminalWindow"),
        {tr("Terminals — Duet"), tr("The terminals are in a window of their own."), tr("Bring the Terminals Back")});

    connect(terminalWindow_, &DetachablePage::detachedChanged, this, [this](bool detached) {
        updateTerminalWindowControls();

        modeTabs_->setTabVisible(static_cast<int>(Mode::Terminal), !detached);
        if (detached && mode_ == Mode::Terminal)
            setMode(modeBeforeTerminal_);
        updateClaudeWatched();
    });

    connect(terminalWindow_, &DetachablePage::windowActivationChanged, this, &MainWindow::updateClaudeWatched);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    toolRequestBar_ = new NoticeBar(central);
    connect(toolRequestBar_, &NoticeBar::actionClicked, this, [this] { answerToolRequest(true); });
    connect(toolRequestBar_, &NoticeBar::dismissed, this, [this] { answerToolRequest(false); });
    centralLayout->addWidget(toolRequestBar_);
    centralLayout->addWidget(modes_, 1);
    setCentralWidget(central);
    createModeTabs();
    commandPalette_ = new CommandPalette(this);
    createPopups();

    FontSettings::applyTreeFont(preferences.treeFont(), this);

    resize(1400, 900);
}

void MainWindow::answerToolRequest(bool accepted)
{
    if (toolRequests_.isEmpty())
        return;
    const ToolRequest request = toolRequests_.takeFirst();
    if (accepted)
        toolRequestBar_->hide();
    request.answer(accepted);
    showToolRequest();
}

void MainWindow::createPopups()
{
    goToPopup_ = new GoToPopup(symbolIndex_, &recentFiles_, this);
    connect(goToPopup_, &GoToPopup::fileChosen, this, [this](const QString &path, int line) {
        setMode(Mode::Edit);
        if (line >= 0)
            navigator_->goToLine(path, line);
        else
            editorTabs_->openFile(path);
    });
    connect(goToPopup_, &GoToPopup::symbolChosen, this, [this](const SymbolLocation &symbol) {
        setMode(Mode::Edit);
        navigator_->goTo(symbol.filePath, symbol.position);
    });
    snapshotsPopup_ = new SnapshotsPopup(gitCommands_, theme_, this);
    newBranchPopup_ = new NewBranchPopup(theme_, this);
    branchesPopup_ = new BranchesPopup(gitActions_, theme_, this);
    connect(branchesPopup_, &BranchesPopup::newBranchRequested, this, &MainWindow::showNewBranch);
    stashesPopup_ = new StashesPopup(gitActions_, theme_, this);
    connect(newBranchPopup_, &NewBranchPopup::createRequested, this, &MainWindow::createBranch);
    newAgentPopup_ = new NewAgentPopup(theme_, this);
    connect(newAgentPopup_, &NewAgentPopup::startRequested, this, &MainWindow::createAgentWorktree);
    closeAgentPopup_ = new CloseAgentPopup(theme_, this);
    connect(closeAgentPopup_, &CloseAgentPopup::fateChosen, this, &MainWindow::closeAgentTab);
    connect(closeAgentPopup_, &CloseAgentPopup::cancelled, this,
            [this](const Worktree &worktree) { stopClosingAgents(tr("%1 stays open").arg(worktree.name)); });
    agentCheckPopup_ = new AgentCheckPopup(theme_, this);
    connect(agentCheckPopup_, &AgentCheckPopup::saved, this, &MainWindow::setAgentCheckCommand);
    diagramRequestPopup_ = new DiagramRequestPopup(theme_, this);
    agentMessage_ = new MessagePopup(QStringLiteral("claude"), QStringLiteral("agentMessage"), theme_, this);
    findInFiles_ = new FindInFilesPopup(theme_, this);
    findInFiles_->setBufferProvider([this] { return editorTabs_->unsavedTexts(); });
    connect(findInFiles_, &FindInFilesPopup::matchChosen, this,
            [this](const QString &path, int line, int column, int length) {
                setMode(Mode::Edit);
                navigator_->goToLine(path, line, column, length);
            });
}

// Edit mode: side panel (Files / Outline / Claude tabs) | editor tabs.
QWidget *MainWindow::createEditPage()
{
    createFilesPanel();
    createClaudeConfigView();
    createProblemsAndOutline();
    QWidget *editorArea = createEditorArea();

    // The tool views as KDevelop's: vertical tabs at the window's edge, the
    // panel next to them.
    sidebar_ = new SidePanel(this);
    sidebar_->addView(filesPanel_, {}, tr("Files"));
    sidebar_->addView(outlineView_, {}, tr("Outline"));
    sidebar_->addView(usagesView_, {}, tr("Usages"));
    sidebar_->addView(hierarchyView_, {}, tr("Hierarchy"));
    sidebar_->addView(problemsView_, {}, tr("Problems"));
    sidebar_->addView(claudeConfigView_, {}, tr("Claude"));
    titleBar_->alignLogoWith(sidebar_->tabBar());

    editSplitter_ = new GripSplitter(Qt::Horizontal, this);
    editSplitter_->setChildrenCollapsible(false);
    editSplitter_->addWidget(sidebar_);
    editSplitter_->addWidget(editorArea);
    editSplitter_->setStretchFactor(1, 1);
    editSplitter_->setSizes({260, 1140});
    sidebar_->foldWithSplitter(editSplitter_);

    auto *page = new QWidget(this);
    auto *pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(sidebar_->strip());
    pageLayout->addWidget(editSplitter_, 1);
    return page;
}

void MainWindow::createFilesPanel()
{
    filesPanel_ = new FilesPanel(this);
    fileTree_ = filesPanel_->tree();
    connect(fileTree_, &FileTreeView::fileActivated, editorTabs_, &EditorTabs::openFile);
    filesPanel_->setAlwaysSelectOpenedFile(QSettings().value(QStringLiteral("alwaysSelectOpenedFile"), false).toBool());
    connect(filesPanel_, &FilesPanel::alwaysSelectOpenedFileChanged, this,
            [](bool enabled) { QSettings().setValue(QStringLiteral("alwaysSelectOpenedFile"), enabled); });
    connect(filesPanel_, &FilesPanel::selectOpenedFileRequested, this, &MainWindow::selectOpenedFile);
    restoreTreeOptions();
    connect(fileTree_, &FileTreeView::statusMessage, this, &MainWindow::showStatus);
    connect(fileTree_, &FileTreeView::entryCreated, this, [this] {
        fileStatuses_->refresh();
        refreshIndexes();
    });
    connect(fileTree_, &FileTreeView::sendToClaudeRequested, this, &MainWindow::sendToClaude);
    connect(fileTree_, &FileTreeView::reviewRequested, this, &MainWindow::reviewPaths);
    connect(fileTree_, &FileTreeView::generateDiagramRequested, this, &MainWindow::generateDiagram);
    connect(fileTree_, &FileTreeView::openTerminalRequested, this,
            [this](const QString &folder) { addTerminal(QFileInfo(folder).fileName(), defaultShell(folder)); });
    connect(fileTree_, &FileTreeView::showDiffRequested, this, [this](const QString &path) {
        setMode(Mode::Diff);
        changesView_->selectFile(projectRelativePath(path));
    });
    connect(fileTree_, &FileTreeView::historyRequested, this, [this](const QString &path) {
        historyView_->setPathFilter(projectRelativePath(path));
        setMode(Mode::History);
    });
    connect(fileTree_, &FileTreeView::stageRequested, this,
            [this](const QString &path) { runGitOnTree(tr("Staging"), path, false); });
    connect(fileTree_, &FileTreeView::rollbackRequested, this,
            [this](const QString &path) { runGitOnTree(tr("Rollback"), path, true); });
    fileStatuses_ = new FileStatusReader(this);
    connect(fileStatuses_, &FileStatusReader::statusesChanged, this,
            [this] { fileTree_->setStatuses(fileStatuses_->statuses(), inRepository_); });
    agentMerges_->watch(fileStatuses_);
    fileTree_->setUnsavedChangesCheck(
        [this](const QString &path) { return editorTabs_->hasUnsavedChangesUnder(path); });
    connect(fileTree_, &FileTreeView::entryRenamed, this, [this](const QString &oldPath, const QString &newPath) {
        editorTabs_->followRename(oldPath, newPath);
        recentFiles_.rename(oldPath, newPath);
        intelligence_->updateReferencesToMovedFile(oldPath, newPath);
        afterFilesChanged();
    });
    connect(fileTree_, &FileTreeView::entryDeleted, this, &MainWindow::forgetDeletedFiles);
    connectTreeToEditor();
}

void MainWindow::restoreTreeOptions()
{
    const QSettings settings;
    ProjectTreeModel::Options treeOptions;
    treeOptions.compactFolders = settings.value(QStringLiteral("tree/compactFolders"), true).toBool();
    treeOptions.nestFiles = settings.value(QStringLiteral("tree/nestFiles"), true).toBool();
    treeOptions.showIgnored = settings.value(QStringLiteral("tree/showIgnored"), true).toBool();
    filesPanel_->setTreeOptions(treeOptions);
    filesPanel_->setFilterWhileTyping(settings.value(QStringLiteral("tree/filterWhileTyping"), false).toBool());
    connect(filesPanel_, &FilesPanel::filterWhileTypingChanged, this,
            [](bool enabled) { QSettings().setValue(QStringLiteral("tree/filterWhileTyping"), enabled); });
    connect(filesPanel_, &FilesPanel::treeOptionsChanged, this, [](const ProjectTreeModel::Options &options) {
        QSettings changed;
        changed.setValue(QStringLiteral("tree/compactFolders"), options.compactFolders);
        changed.setValue(QStringLiteral("tree/nestFiles"), options.nestFiles);
        changed.setValue(QStringLiteral("tree/showIgnored"), options.showIgnored);
    });
}

void MainWindow::createClaudeConfigView()
{
    claudeConfigView_ = new ClaudeConfigView(this);
    connect(claudeConfigView_, &ClaudeConfigView::fileRequested, this, [this](const QString &path, bool readOnly) {
        editorTabs_->openFile(path);
        if (EditorWidget *editor = editorTabs_->editorFor(path); editor && readOnly)
            editor->setReadOnly(true);
    });
    connect(claudeConfigView_, &ClaudeConfigView::sendToClaudeRequested, this, &MainWindow::sendToClaude);
    claudeConfigView_->setUnsavedChangesCheck(
        [this](const QString &path) { return editorTabs_->hasUnsavedChangesUnder(path); });
    connect(claudeConfigView_, &ClaudeConfigView::entryDeleted, this, &MainWindow::forgetDeletedFiles);
    connect(claudeConfigView_, &ClaudeConfigView::restartSessionRequested, this, &MainWindow::restartClaudeSession);
    connect(claudeConfigView_, &ClaudeConfigView::statusMessage, this, &MainWindow::showStatus);
    connect(editorTabs_, &EditorTabs::fileSaved, claudeConfigView_, &ClaudeConfigView::scheduleRefresh);
    // Settings, skills and agents checked as they are edited.
    connect(editorTabs_, &EditorTabs::editorOpened, new ClaudeLinter(this), &ClaudeLinter::attach);
    connect(claudeConfigView_, &ClaudeConfigView::effectiveSettingsRequested, this,
            [this] { showEffectiveSettings(true); });
    // An open Effective Settings tab follows the files.
    connect(claudeConfigView_, &ClaudeConfigView::refreshed, this, [this] { showEffectiveSettings(false); });
}

void MainWindow::createProblemsAndOutline()
{
    outlineView_ = new OutlineView(this);
    problemsView_ = new ProblemsView(problemStore_, this);
    connect(problemsView_, &ProblemsView::problemActivated, this, [this](const QString &path, int line, int column) {
        setMode(Mode::Edit);
        navigator_->goToLine(path, line, std::max(column, 0));
    });
    connect(problemsView_, &ProblemsView::countsChanged, this, &MainWindow::updateTabIcons);
    usagesView_ = new UsagesView(this);
    connect(usagesView_, &UsagesView::usageActivated, this, [this](const QString &path, int line, int column) {
        setMode(Mode::Edit);
        navigator_->goToLine(path, line, column);
    });
    connect(usagesView_, &UsagesView::rerunRequested, intelligence_, &CodeIntelligence::findUsagesOf);
    hierarchyView_ = new HierarchyView(this);
    connect(hierarchyView_, &HierarchyView::elementActivated, this, [this](const QString &path, int line, int column) {
        setMode(Mode::Edit);
        navigator_->goToLine(path, line, column);
    });
    connect(intelligence_, &CodeIntelligence::hierarchyFound, this, [this](const Hierarchy &hierarchy) {
        hierarchyView_->showHierarchy(hierarchy);
        sidebar_->showView(hierarchyView_);
    });
    connect(intelligence_, &CodeIntelligence::usagesFound, this, [this](const UsageResults &results) {
        usagesView_->showResults(results);
        sidebar_->showView(usagesView_);
    });
    connect(editorTabs_, &QTabWidget::currentChanged, this,
            [this] { outlineView_->setEditor(editorTabs_->currentEditor()); });
    connect(outlineView_, &OutlineView::symbolActivated, editorTabs_, &EditorTabs::scrollPreviewToCaret);
    connect(editorTabs_, &QTabWidget::currentChanged, this, &MainWindow::onCurrentTabChanged);
}

QWidget *MainWindow::createEditorArea()
{
    // Find bar above the editor tabs, hidden until Ctrl+F.
    findBar_ = new FindBar(this);
    auto *editorArea = new QWidget(this);
    auto *editorLayout = new QVBoxLayout(editorArea);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);
    editorLayout->addWidget(findBar_);
    // Tools of the project's setup not downloaded yet.
    toolsNotice_ = new NoticeBar(editorArea);
    connect(toolsNotice_, &NoticeBar::actionClicked, this, [this] { projectSettings()->download(project_); });
    editorLayout->addWidget(toolsNotice_);
    // A language server that cannot run, for the current file's language.
    serverNotice_ = new NoticeBar(editorArea);
    connect(serverNotice_, &NoticeBar::actionClicked, this, [this] {
        if (const EditorWidget *editor = editorTabs_->currentEditor())
            languageServers_->retry(editor->languageId());
    });
    connect(serverNotice_, &NoticeBar::dismissed, this, [this] {
        if (const EditorWidget *editor = editorTabs_->currentEditor())
            dismissedServerNotices_.insert(languageServers_->failure(editor->languageId()));
    });
    connect(languageServers_, &LanguageServers::serverFailed, this, &MainWindow::updateServerNotice);
    editorLayout->addWidget(serverNotice_);
    // Claude was kept from overwriting unsaved changes.
    unsavedEditNotice_ = new NoticeBar(editorArea);
    connect(unsavedEditNotice_, &NoticeBar::actionClicked, this, [this] {
        if (editorTabs_->saveAllNow())
            unsavedEditNotice_->hide();
    });
    editorLayout->addWidget(unsavedEditNotice_);
    editorLayout->addWidget(editorTabs_, 1);
    return editorArea;
}

void MainWindow::forgetDeletedFiles(const QString &path)
{
    editorTabs_->closeFilesUnder(path);
    recentFiles_.remove(path);
    afterFilesChanged();
}

void MainWindow::afterFilesChanged()
{
    fileTree_->setClaudeEdited(recentFiles_.editedByClaude());
    fileStatuses_->refresh();
    refreshIndexes();
}

void MainWindow::refreshIndexes()
{
    symbolIndex_->refresh();
    occurrenceIndex_->refresh();
}

void MainWindow::showStatus(const QString &message)
{
    statusBar()->showMessage(message, statusMessageMs);
}

QString MainWindow::projectRelativePath(const QString &path) const
{
    const QString relative = QDir(folder_).relativeFilePath(path);
    return relative == QLatin1String(".") ? QString() : relative;
}

// Terminal mode: the terminal tabs, or a hint as long as there are none.
QWidget *MainWindow::createTerminalPage()
{
    terminalTabs_ = new QTabWidget(this);
    terminalTabs_->setObjectName(QStringLiteral("terminalTabs"));
    terminalTabs_->setTabsClosable(true);
    terminalTabs_->setDocumentMode(true);
    terminalTabs_->tabBar()->setDrawBase(false);
    new HoverCloseButtons(terminalTabs_->tabBar());
    connect(terminalTabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget *terminal = terminalTabs_->widget(index);
        if (terminal == claudeTerminal_)
            return; // the Claude session stays for the lifetime of the folder
        if (const AgentSessions::Agent *agent = agentSessions_.find(terminal)) {
            closeAgentSession(agent->terminal);
            return;
        }
        terminalTabs_->removeTab(index);
        terminal->deleteLater();
        updateTerminalPage();
    });
    connect(terminalTabs_, &QTabWidget::currentChanged, this, &MainWindow::updateClaudeWatched);

    newTerminalButton_ = new QToolButton(terminalTabs_);
    newTerminalButton_->setAutoRaise(true);
    newTerminalButton_->setFocusPolicy(Qt::NoFocus);
    newTerminalButton_->setToolTip(tr("New Terminal (Ctrl+Shift+T)"));
    connect(newTerminalButton_, &QToolButton::clicked, this, &MainWindow::newShell);
    auto *trailing = new TrailingTabButton(terminalTabs_, newTerminalButton_); // right after the last tab
    // The terminals, tabs and all, to a window of their own, on any screen.
    detachTerminalsButton_ = new QToolButton(terminalTabs_);
    detachTerminalsButton_->setAutoRaise(true);
    detachTerminalsButton_->setFocusPolicy(Qt::NoFocus);
    connect(detachTerminalsButton_, &QToolButton::clicked, this,
            [this] { setTerminalsDetached(!terminalWindow_->isDetached()); });
    trailing->setEndButton(detachTerminalsButton_);

    auto *hint = new QLabel(tr("No terminal open\n\n"
                               "Ctrl+Shift+L: start the agent's session"),
                            this);
    hint->setAlignment(Qt::AlignCenter);
    hint->setEnabled(false);
    emptyNewTerminalButton_ = new QToolButton(this);
    emptyNewTerminalButton_->setText(tr("New Terminal"));
    emptyNewTerminalButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    emptyNewTerminalButton_->setAutoRaise(true);
    emptyNewTerminalButton_->setFocusPolicy(Qt::NoFocus);
    emptyNewTerminalButton_->setToolTip(tr("New Terminal (Ctrl+Shift+T)"));
    connect(emptyNewTerminalButton_, &QToolButton::clicked, this, &MainWindow::newShell);

    auto *placeholder = new QWidget(this);
    auto *placeholderLayout = new QVBoxLayout(placeholder);
    placeholderLayout->addStretch();
    placeholderLayout->addWidget(hint);
    placeholderLayout->addSpacing(8);
    placeholderLayout->addWidget(emptyNewTerminalButton_, 0, Qt::AlignHCenter);
    placeholderLayout->addStretch();

    terminalPage_ = new QStackedWidget(this);
    terminalPage_->addWidget(placeholder);
    terminalPage_->addWidget(terminalTabs_);
    return terminalPage_;
}

void MainWindow::createModeTabs()
{
    modeTabs_ = new ModeTabBar(this);
    modeTabs_->setObjectName(QStringLiteral("modeTabs"));
    modeTabs_->setDocumentMode(true);
    modeTabs_->setDrawBase(false);
    modeTabs_->setExpanding(false);
    modeTabs_->setFocusPolicy(Qt::NoFocus);
    modeTabs_->setShape(QTabBar::RoundedSouth);

    modeTabs_->setUsesScrollButtons(false);
    modeTabs_->setElideMode(Qt::ElideNone);
    modeTabs_->addTab(tr("Terminal"));
    modeTabs_->addTab(tr("Edit"));
    modeTabs_->addTab(tr("Review"));
    modeTabs_->addTab(tr("Diff"));
    modeTabs_->addTab(tr("History"));
    modeTabs_->setTabToolTip(int(Mode::Terminal), tr("Full-screen terminals (Ctrl+1)"));
    modeTabs_->setTabToolTip(int(Mode::Edit), tr("File tree and code editor (Ctrl+2)"));
    modeTabs_->setTabToolTip(
        int(Mode::Review), tr("A separate session reviews the staged changes before the diff and the commit (Ctrl+5)"));
    modeTabs_->setTabToolTip(int(Mode::Diff), tr("Pending changes, staging and commit (Ctrl+3)"));
    modeTabs_->setTabToolTip(int(Mode::History), tr("Past commits, their files and their diffs (Ctrl+4)"));
    modeTabs_->setTabVisible(int(Mode::Review), false); // see setReviewStep()
    connect(modeTabs_, &QTabBar::currentChanged, this, [this](int index) { setMode(static_cast<Mode>(index)); });
    createStatusBar();
}

void MainWindow::createStatusBar()
{
    auto *bar = new WorkspaceStatusBar(modeTabs_, this);
    setStatusBar(bar);
    agentIndicator_ = new AgentIndicator(this);
    agentIndicator_->applyTheme(theme_);
    agentIndicator_->alignWith(sidebar_->tabBar());
    statusBar()->addWidget(agentIndicator_);
    agentIndicator_->hide();
    connect(agentIndicator_, &AgentIndicator::clicked, this, &MainWindow::showClaudeSession);
    connect(statusBar(), &QStatusBar::messageChanged, this, &MainWindow::updateAgentIndicator);

    syncButton_ = new QToolButton(this);
    syncButton_->setAutoRaise(true);
    syncButton_->setFocusPolicy(Qt::NoFocus);
    syncButton_->hide();
    connect(syncButton_, &QToolButton::clicked, gitActions_, &GitActions::sync);

    branchButton_ = new ElidingToolButton(this, 220);
    branchButton_->setObjectName(QStringLiteral("chip"));
    branchButton_->setAutoRaise(true);
    branchButton_->setFocusPolicy(Qt::NoFocus);
    branchButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    branchButton_->hide();
    connect(branchButton_, &QToolButton::clicked, this, &MainWindow::showBranches);

    cancelGitButton_ = new QToolButton(this);
    cancelGitButton_->setAutoRaise(true);
    cancelGitButton_->setFocusPolicy(Qt::NoFocus);
    cancelGitButton_->setToolTip(tr("Stop the git operation"));
    cancelGitButton_->hide();
    connect(cancelGitButton_, &QToolButton::clicked, gitCommands_, &GitCommands::cancel);
    usageLabel_ = new QLabel(this);
    usageLabel_->setContentsMargins(8, 0, 8, 0);
    usageLabel_->hide();
    editorStatus_ = new EditorStatus(this);
    auto *serverTasks = new ServerTasksLabel(languageServers_, this);
    auto *refactoring = new BusyIndicator(this);
    connect(intelligence_, &CodeIntelligence::refactoringStarted, refactoring, &BusyIndicator::start);
    connect(intelligence_, &CodeIntelligence::refactoringFinished, refactoring, &BusyIndicator::finish);

    bar->addGroup(tr("Refactoring"), {refactoring});
    bar->addGroup(tr("Language Servers"), {serverTasks});
    bar->addGroup(tr("Agent Usage"), {usageLabel_});
    bar->addGroup(tr("Editor"), {editorStatus_});
    bar->addGroup(tr("Git"), {branchButton_, syncButton_, cancelGitButton_});
    for (QToolButton *button : {syncButton_, branchButton_, cancelGitButton_}) {
        UiMetrics::setControlHeight(button);
        UiMetrics::setScaledIconSize(button);
    }
}

void MainWindow::restoreSettings()
{
    const QSettings settings;
    restoreTitleBarFrame(settings);
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    editSplitter_->restoreState(settings.value(QStringLiteral("editSplitter")).toByteArray());
    changesView_->splitter()->restoreState(settings.value(QStringLiteral("diffSplitter")).toByteArray());
    changesView_->fileSplitter()->restoreState(settings.value(QStringLiteral("diffFileSplitter")).toByteArray());
    changesView_->setFileListVisible(settings.value(QStringLiteral("diffFileListVisible"), true).toBool());
    historyView_->restoreState(settings.value(QStringLiteral("historySplitter")).toByteArray());
    sidebar_->tabBar()->setCurrentIndex(settings.value(QStringLiteral("sidebarTab"), 0).toInt());
    sidebar_->setPanelVisible(showSidebar_->isChecked());
    setTheme(savedThemeName());
}

void MainWindow::restoreTitleBarFrame(const QSettings &settings)
{
    const bool system = settings.value(QStringLiteral("systemTitleBar"), false).toBool();

    const QSignalBlocker blocker(systemTitleBar_);
    systemTitleBar_->setChecked(system);
    setSystemTitleBar(system);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmCloseWhileWorking()) {
        event->ignore();
        return;
    }
    const bool hasAgentWork = !agentSessions_.isEmpty() || agentMerges_->isMerging();
    if (isClosingAgents_ || hasAgentWork) {
        event->ignore();
        closeAgentsThenWindow();
        return;
    }
    saveSession();
    // Closed or cancelled at the unsaved files: the next close asks again.
    closeConfirmation_ = CloseConfirmation::NotAsked;
    if (!editorTabs_->closeAll()) {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("editSplitter"), editSplitter_->saveState());
    settings.setValue(QStringLiteral("diffSplitter"), changesView_->splitter()->saveState());
    settings.setValue(QStringLiteral("diffFileSplitter"), changesView_->fileSplitter()->saveState());
    settings.setValue(QStringLiteral("diffFileListVisible"), changesView_->isFileListVisible());
    settings.setValue(QStringLiteral("historySplitter"), historyView_->saveState());
    settings.setValue(QStringLiteral("showSidebar"), sidebar_->isPanelVisible());
    settings.setValue(QStringLiteral("sidebarTab"), sidebar_->tabBar()->currentIndex());
    settings.setValue(QStringLiteral("mode"), nameOf(mode_));
    settings.setValue(QStringLiteral("lastFolder"), folder_);

    if (closingProject_)
        emit projectClosed();
    event->accept();
}

void MainWindow::setMode(Mode mode)
{
    if (mode == Mode::Terminal && terminalWindow_->isDetached()) {
        if (modes_->currentIndex() == static_cast<int>(Mode::Terminal))
            setMode(modeBeforeTerminal_);
        else if (modeActions_)
            modeActions_->actions().at(static_cast<int>(mode_))->setChecked(true);
        showTerminals();
        return;
    }
    if (mode == Mode::Review && !isReviewStepOn())
        mode = Mode::Diff;
    if (mode != Mode::Terminal)
        modeBeforeTerminal_ = mode;
    mode_ = mode;
    const int index = static_cast<int>(mode);
    modes_->setCurrentIndex(index);
    {
        const QSignalBlocker blocker(modeTabs_);
        modeTabs_->setCurrentIndex(index);
    }
    if (modeActions_)
        modeActions_->actions().at(index)->setChecked(true);
    editorStatus_->setEditor(mode == Mode::Edit ? editorTabs_->currentEditor() : nullptr);

    switch (mode) {
    case Mode::Diff:
        changesView_->refresh();
        break;
    case Mode::History:
        historyView_->refresh();
        break;
    case Mode::Review:
        reviewPage_->setFocus();
        break;
    case Mode::Edit:
        if (QWidget *editor = editorTabs_->currentWidget())
            editor->setFocus();
        break;
    case Mode::Terminal:
        if (QWidget *terminal = terminalTabs_->currentWidget())
            terminal->setFocus();
        break;
    }
    updateClaudeWatched();
}

// F11: to the terminal, then back to the previous mode.
void MainWindow::toggleTerminalMode()
{
    setMode(mode_ == Mode::Terminal ? modeBeforeTerminal_ : Mode::Terminal);
}

// Leaving full screen gives the window back maximized if it was.
void MainWindow::toggleFullScreen()
{
    setWindowState(windowState() ^ Qt::WindowFullScreen);
}

// Ctrl+Shift+O: Outline panel, with the filter ready for typing.
void MainWindow::showOutline()
{
    setMode(Mode::Edit);
    sidebar_->showView(outlineView_);
    outlineView_->focusFilter();
}

void MainWindow::updateTerminalPage()
{
    terminalPage_->setCurrentIndex(terminalTabs_->count() > 0 ? 1 : 0);
}

void MainWindow::showTerminals()
{
    if (terminalWindow_->isDetached()) {
        terminalWindow_->raiseWindow();
        if (QWidget *terminal = terminalTabs_->currentWidget())
            terminal->setFocus();
        return;
    }
    setMode(Mode::Terminal);
}

void MainWindow::setTerminalsDetached(bool detached)
{
    if (detached)
        terminalWindow_->detach();
    else
        terminalWindow_->attach();
}

void MainWindow::updateTerminalWindowControls()
{
    const bool detached = terminalWindow_->isDetached();
    if (detachTerminalsAction_) {
        const QSignalBlocker blocker(detachTerminalsAction_);
        detachTerminalsAction_->setChecked(detached);
    }
    detachTerminalsButton_->setIcon(
        UiIcons::icon(detached ? QStringLiteral("attach") : QStringLiteral("detach"), theme_.mutedText));
    detachTerminalsButton_->setToolTip(detached ? tr("Bring the terminals back to the main window")
                                                : tr("Move the terminals to a window of their own"));
}

// Line icons of the modes, side panel and terminals, in the theme's colors.
void MainWindow::updateTabIcons()
{
    const QColor color = theme_.mutedText;
    constexpr std::array<const char *, 5> modes{"terminal", "code", "check", "diff", "history"};
    for (std::size_t i = 1; i < modes.size(); ++i)
        modeTabs_->setTabIcon(static_cast<int>(i), UiIcons::icon(QLatin1String(modes.at(i)), color));
    // Hidden, QTabBar does not report that its icons widened it: the status
    // bar would keep its old width and cut the last tabs.
    modeTabs_->updateGeometry();
    sidebar_->setViewIcon(filesPanel_, UiIcons::icon(QStringLiteral("files"), color));
    sidebar_->setViewIcon(outlineView_, UiIcons::icon(QStringLiteral("outline"), color));
    sidebar_->setViewIcon(claudeConfigView_, UiIcons::icon(QStringLiteral("claude"), color));
    sidebar_->setViewIcon(problemsView_, UiIcons::icon(QStringLiteral("problems"),
                                                       problemsIconColor(problemsView_->errorCount(),
                                                                         problemsView_->warningCount(), theme_)));
    newTerminalButton_->setIcon(UiIcons::icon(QStringLiteral("plus"), color));
    syncButton_->setIcon(UiIcons::icon(QStringLiteral("refresh"), color));
    cancelGitButton_->setIcon(UiIcons::icon(QStringLiteral("stop"), color));
    emptyNewTerminalButton_->setIcon(UiIcons::icon(QStringLiteral("plus"), color));
    updateTerminalWindowControls();
    for (int i = 0; i < terminalTabs_->count(); ++i) {
        const QWidget *terminal = terminalTabs_->widget(i);
        if (terminal == claudeTerminal_)
            continue; // its icon shows Claude's state
        if (const AgentSessions::Agent *agent = agentSessions_.find(terminal)) {
            updateAgentTab(agent->terminal, agent->activity->state());
            continue;
        }
        // A run's: its kind, colored by its outcome.
        const QString runIcon = terminal->property("runIcon").toString();
        const auto runColor = terminal->property("runColor").value<QColor>();
        terminalTabs_->setTabIcon(i, UiIcons::icon(runIcon.isEmpty() ? QStringLiteral("terminal") : runIcon,
                                                   runColor.isValid() ? runColor : color));
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        fitTitleBarToWindowState();
        fullScreenAction_->setChecked(isFullScreen());
    }
    if (event->type() == QEvent::ActivationChange) {
        if (isActiveWindow())
            clearNotifications();
        if (isActiveWindow()) // the configuration may have changed elsewhere
            claudeConfigView_->scheduleRefresh();
        updateClaudeWatched();
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const auto *key = event->type() == QEvent::KeyPress ? dynamic_cast<const QKeyEvent *>(event) : nullptr;
    if (watched == claudeTerminal_ && key) {
#ifdef Q_OS_MACOS
        const Qt::KeyboardModifiers control = Qt::MetaModifier; // the Control key; Cmd+C copies
#else
        const Qt::KeyboardModifiers control = Qt::ControlModifier;
#endif
        // Ctrl+C twice would quit Claude and leave the tab dead: it
        // becomes Escape, which interrupts the response.
        if (key->key() == Qt::Key_C && key->modifiers() == control) {
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(claudeTerminal_, &escape);
            return true;
        }
        if (key->key() == Qt::Key_Escape)
            agentActivity_.interrupt();
    }
    return QMainWindow::eventFilter(watched, event);
}

QString MainWindow::absolutePath(const QString &relativePath) const
{
    return QDir(folder_).filePath(relativePath);
}
