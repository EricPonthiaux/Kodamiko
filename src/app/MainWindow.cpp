#include "MainWindow.h"

#include "changes/BranchesPopup.h"
#include "changes/ChangesView.h"
#include "changes/GitActions.h"
#include "changes/NewBranchPopup.h"
#include "changes/SnapshotsPopup.h"
#include "changes/StashesPopup.h"
#include "claude/ClaudeConfigView.h"
#include "claude/ClaudeLauncher.h"
#include "claude/ClaudeLinter.h"
#include "claude/ClaudeUsage.h"
#include "claude/DiagramRequestPopup.h"
#include "claude/EditorContext.h"
#include "claude/EffectiveSettings.h"
#include "claude/UnsavedEditGuard.h"
#include "claude/mcp/DuetTools.h"
#include "completion/CodeGeneration.h"
#include "completion/CompletionController.h"
#include "completion/CompletionService.h"
#include "completion/DocComments.h"
#include "completion/FullLine.h"
#include "completion/LocalCompletion.h"
#include "completion/NameSuggestions.h"
#include "completion/Templates.h"
#include "diagnostics/ProblemStore.h"
#include "diagnostics/ProblemsView.h"
#include "editor/ChangeGutter.h"
#include "editor/EditorStatus.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/FindBar.h"
#include "git/Askpass.h"
#include "git/Branches.h"
#include "git/FileStatuses.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"
#include "git/Integration.h"
#include "git/RepositoryStatus.h"
#include "git/RepositoryWatcher.h"
#include "git/Snapshots.h"
#include "history/HistoryView.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspCompletionProvider.h"
#include "navigation/CodeIntelligence.h"
#include "navigation/CodeNavigator.h"
#include "navigation/GoToPopup.h"
#include "navigation/SymbolIndex.h"
#include "outline/OutlineView.h"
#include "projects/ProjectStore.h"
#include "run/RunConfiguration.h"
#include "run/RunConfigurationMenu.h"
#include "run/Runner.h"
#include "search/FindInFilesPopup.h"
#include "settings/Keymap.h"
#include "settings/Preferences.h"
#include "settings/PreferencesPopup.h"
#include "setup/PrerequisitesDialog.h"
#include "terminal/TerminalWidget.h"
#include "toolchains/InstallPlan.h"
#include "toolchains/Toolchains.h"
#include "ui/GripSplitter.h"
#include "ui/NoticeBar.h"
#include "ui/ThemeStyle.h"
#include "ui/TitleBar.h"
#include "ui/TrailingLayout.h"
#include "ui/TrailingTabButton.h"
#include "ui/TreeMetrics.h"
#include "ui/UiIcons.h"
#include "ui/WindowFrame.h"
#include "viewers/DiagramKinds.h"
#include "welcome/ProjectSettingsPopup.h"
#include "welcome/ProjectSetupForm.h"
#include "workspace/CommandPalette.h"
#include "workspace/FileTreeView.h"
#include "workspace/FilesPanel.h"
#include "workspace/SidePanel.h"
#ifdef DUET_WITH_DIAGRAMS
#include "viewers/DiagramCheck.h"
#include "viewers/DiagramGenerationView.h"
#include "viewers/DiagramView.h"
#endif

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <array>
#include <ranges>

namespace {

constexpr int statusMessageMs = 5000;
// Between a prompt pasted in Claude's terminal and the Enter that sends it.
constexpr int submitDelayMs = 150;
constexpr int notificationMs = 10000;
constexpr qsizetype snapshotPromptLength = 60;

// Dot drawn in front of the Claude tabs: amber while Claude works, blue
// when it waits for the user, green when a response is ready.
QIcon activityIcon(ClaudeActivity::State state, const Theme &theme)
{
    const bool dark = theme.isDark;
    QColor color;
    switch (state) {
    case ClaudeActivity::State::Idle:
        return {};
    case ClaudeActivity::State::Working:
        color = QColor(dark ? "#e5c07b" : "#c18401");
        break;
    case ClaudeActivity::State::NeedsInput:
        color = QColor(dark ? "#61afef" : "#4078f2");
        break;
    case ClaudeActivity::State::Done:
        color = QColor(dark ? "#98c379" : "#50a14f");
        break;
    }
    QIcon icon;
    for (const qreal ratio : {1.0, 2.0}) {
        QPixmap pixmap(QSize(16, 16) * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QRectF(4, 4, 8, 8));
        icon.addPixmap(pixmap);
    }
    return icon;
}

// The folder asked for, else the last one opened, else the home folder.
QString startFolder(const QString &requested)
{
    if (!requested.isEmpty())
        return requested;
    QString lastFolder = QSettings().value(QStringLiteral("lastFolder")).toString();
    if (!lastFolder.isEmpty())
        return lastFolder;
    return QDir::homePath();
}

QLatin1String runKindIconName(RunConfiguration::Kind kind)
{
    switch (kind) {
    case RunConfiguration::Kind::Build:
        return QLatin1String("build");
    case RunConfiguration::Kind::Run:
        return QLatin1String("run");
    case RunConfiguration::Kind::Test:
        return QLatin1String("test");
    }
    return QLatin1String("run");
}

// Exit code -1: stopped, no outcome color.
QColor runOutcomeColor(int exitCode)
{
    if (exitCode == -1)
        return {};
    return exitCode == 0 ? QColor("#4fa36b") : QColor("#e5534b");
}

QString runFinishedMessage(const RunConfiguration &configuration, int exitCode, int errors, int warnings)
{
    if (exitCode == -1)
        return MainWindow::tr("“%1” stopped").arg(configuration.name);
    QStringList found;
    if (errors > 0)
        found.append(MainWindow::tr("%n error(s)", nullptr, errors));
    if (warnings > 0)
        found.append(MainWindow::tr("%n warning(s)", nullptr, warnings));
    const QString foundList = found.join(QStringLiteral(", "));
    if (exitCode == 0) {
        return found.isEmpty() ? MainWindow::tr("“%1” finished").arg(configuration.name)
                               : MainWindow::tr("“%1” finished: %2").arg(configuration.name, foundList);
    }
    if (!found.isEmpty())
        return MainWindow::tr("“%1” failed: %2").arg(configuration.name, foundList);
    return MainWindow::tr("“%1” failed (exit code %2): F11 shows its output").arg(configuration.name).arg(exitCode);
}

// `sorted` by start, not empty; past the last problem, around to the first.
const Diagnostic &problemAfter(const Diagnostics &sorted, int cursor)
{
    const auto found =
        std::ranges::find_if(sorted, [cursor](const Diagnostic &problem) { return problem.start > cursor; });
    return found != sorted.end() ? *found : sorted.first();
}

// `sorted` by start, not empty; before the first problem, around to the last.
const Diagnostic &problemBefore(const Diagnostics &sorted, int cursor)
{
    const auto reversed = sorted | std::views::reverse;
    const auto found =
        std::ranges::find_if(reversed, [cursor](const Diagnostic &problem) { return problem.start < cursor; });
    return found != reversed.end() ? *found : sorted.last();
}

// Usage of the Claude plan: red from 90%, amber from 75%, none below.
const char *usageWarningColor(double highestPercent, bool dark)
{
    if (highestPercent >= 90)
        return dark ? "#e06c75" : "#e45649";
    if (highestPercent >= 75)
        return dark ? "#e5c07b" : "#c18401";
    return nullptr;
}

QColor problemsIconColor(int errorCount, int warningCount, const QColor &noProblemColor)
{
    if (errorCount > 0)
        return {"#e5534b"};
    if (warningCount > 0)
        return {"#d4a72c"};
    return noProblemColor;
}

// The changed files git knows under `path`, as repository paths: untracked
// and ignored files stay.
QStringList trackedChangesUnder(const QString &path,
                                const FileStatusMap &statuses,
                                const RepositoryInfo &info,
                                const QDir &folder)
{
    QStringList paths;
    for (auto it = statuses.cbegin(); it != statuses.cend(); ++it) {
        const bool isTracked = it.value() != FileStatus::Untracked && it.value() != FileStatus::Ignored;
        const bool isUnderPath = it.key() == path || it.key().startsWith(path + QLatin1Char('/'));
        if (isTracked && isUnderPath)
            paths.append(info.repoPath(folder.relativeFilePath(it.key())));
    }
    return paths;
}

// `path` as git names it; the project's own folder is its prefix in the
// repository, or "." at its root.
QString repositoryPathOf(const QString &path, const RepositoryInfo &info, const QDir &folder)
{
    const QString relative = folder.relativeFilePath(path);
    if (relative != QLatin1String("."))
        return info.repoPath(relative);
    return info.prefix.isEmpty() ? QStringLiteral(".") : info.prefix;
}

} // namespace

MainWindow::MainWindow(const QString &folder, QWidget *parent)
    : QMainWindow(parent)
    , theme_(Theme::named(savedThemeName()))
{
    // The saved theme from the start: the widgets are styled as they are
    // created, rather than all restyled when restoreSettings() applies it.
    Preferences::load().applyTypography(theme_);
    ThemeStyle::apply(theme_);
    createWidgets();
    createMenus();
    applyKeymap(Keymap::loadOverrides());
    restoreSettings();

    connect(&hookServer_, &HookServer::hookReceived, this, &MainWindow::onHookReceived);
    registerMcpTools();
    connect(&claudeActivity_, &ClaudeActivity::stateChanged, this, &MainWindow::updateClaudeIndicator);
    connect(&claudeActivity_, &ClaudeActivity::attentionRequested, this, &MainWindow::notify);
    if (!hookServer_.listen())
        statusBar()->showMessage(tr("Claude tracking unavailable: %1").arg(hookServer_.errorString()));

    openFolder(startFolder(folder));
    setMode(static_cast<Mode>(
        std::clamp(QSettings().value(QStringLiteral("viewMode"), static_cast<int>(Mode::Edit)).toInt(), 0, 3)));

    // New installation: say what is missing rather than failing later.
    if (!Prerequisites::allFound())
        QTimer::singleShot(0, this, &MainWindow::showPrerequisites);
}

MainWindow::~MainWindow()
{
    // Its downloads use the toolchains, destroyed before the children.
    delete projectSettings_;
}

void MainWindow::createWidgets()
{
    // Duet's title bar in place of the menu bar: it holds the menus.
    titleBar_ = new TitleBar(this);
    setMenuWidget(titleBar_);
    connect(titleBar_, &TitleBar::claudeClicked, this, &MainWindow::showClaudeSession);
    frame_ = new WindowFrame(this);
    projectMenu_ = new QMenu(this);

    editorTabs_ = new EditorTabs(theme_, this);
    editorTabs_->setEditorOptions(Preferences::load().editor);
    connect(editorTabs_, &EditorTabs::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });

    // Symbol index for "go to definition": reread after every
    // save, and when returning to the application (files modified elsewhere).
    symbolIndex_ = new SymbolIndex(this);
    connect(editorTabs_, &EditorTabs::fileSaved, symbolIndex_, &SymbolIndex::refresh);

    // Completion: the language server, project symbols, keywords, words
    // of the file.
    toolchains_ = std::make_unique<Toolchains>();
    languageServers_ = new LanguageServers(this);
    languageServers_->setEnabled(Preferences::load().languageServers);
    connect(languageServers_, &LanguageServers::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });
    // Problems: the servers', and those read in the output of the runs.
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
    connect(fullLineModel_, &LocalFullLineModel::failed, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });
    applyFullLine(Preferences::load());
    connect(editorTabs_, &EditorTabs::editorOpened, this, [this](EditorWidget *editor) {
        editor->setFileActions(codeRequestActions_);
        const auto *controller = new CompletionController(editor, completion_);
        // The parameters of the function just chosen.
        connect(controller, &CompletionController::callInserted, this, [this, editor] {
            if (intelligence_)
                intelligence_->requestParameterInfo(editor);
        });
        // Once the file is loaded and the tab shown.
        QTimer::singleShot(0, editor, [this, editor] { warmUpLanguageServer(editor); });
    });
    connect(editorTabs_, &EditorTabs::fileSaved, this, [this] { fileStatuses_->refresh(); });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {
            symbolIndex_->refresh();
            fileStatuses_->refresh();
        }
    });
    navigator_ = new CodeNavigator(editorTabs_, symbolIndex_, this);
    // The language server first (documentation, usages, rename...), the
    // project's index when there is none.
    intelligence_ = new CodeIntelligence(editorTabs_, languageServers_, navigator_, problemStore_, this);
    connect(editorTabs_, &EditorTabs::editorOpened, intelligence_, &CodeIntelligence::attach);
    connect(editorTabs_, &EditorTabs::definitionRequested, intelligence_, &CodeIntelligence::goToDefinition);
    connect(intelligence_, &CodeIntelligence::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });
    connect(navigator_, &CodeNavigator::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });

    createGitServices();

    changesView_ = new ChangesView(theme_, gitCommands_, this);
    connect(changesView_, &ChangesView::openFileRequested, this, [this](const QString &relativePath) {
        setMode(Mode::Edit);
        editorTabs_->openFile(absolutePath(relativePath));
    });
    connect(changesView_, &ChangesView::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });
    connect(changesView_, &ChangesView::continueRequested, gitActions_, &GitActions::continueOperation);
    connect(changesView_, &ChangesView::skipRequested, gitActions_, &GitActions::skipCommit);
    connect(changesView_, &ChangesView::abortRequested, gitActions_, &GitActions::abortOperation);

    historyView_ = new HistoryView(theme_, this);
    connect(historyView_, &HistoryView::checkoutRequested, gitActions_, &GitActions::checkoutCommit);
    connect(historyView_, &HistoryView::openFileRequested, this, [this](const QString &relativePath) {
        setMode(Mode::Edit);
        editorTabs_->openFile(absolutePath(relativePath));
    });

    // Same order as the Mode enum.
    modes_ = new QStackedWidget(this);
    modes_->addWidget(createTerminalPage());
    modes_->addWidget(createEditPage());
    modes_->addWidget(changesView_);
    modes_->addWidget(historyView_);
    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    toolRequestBar_ = new NoticeBar(central);
    connect(toolRequestBar_, &NoticeBar::actionClicked, this, [this] {
        if (toolRequests_.isEmpty())
            return;
        const ToolRequest request = toolRequests_.takeFirst();
        toolRequestBar_->hide();
        request.answer(true);
        showToolRequest();
    });
    connect(toolRequestBar_, &NoticeBar::dismissed, this, [this] {
        if (toolRequests_.isEmpty())
            return;
        const ToolRequest request = toolRequests_.takeFirst();
        request.answer(false);
        showToolRequest();
    });
    centralLayout->addWidget(toolRequestBar_);
    centralLayout->addWidget(modes_, 1);
    setCentralWidget(central);
    createModeTabs();
    commandPalette_ = new CommandPalette(this);
    createPopups();

    applyTreeFont(Preferences::load().treeFont());

    resize(1400, 900);
}

void MainWindow::createGitServices()
{
    // Git operations: result in the status bar, errors (hook output
    // included) in a dialog box.
    gitCommands_ = new GitCommands(this);
    connect(gitCommands_, &GitCommands::succeeded, this, [this](const QString &, const QString &message) {
        if (!message.isEmpty())
            statusBar()->showMessage(message, statusMessageMs);
        updateRepositoryInfo();
    });
    connect(gitCommands_, &GitCommands::failed, this, [this](const QString &title, const QString &error) {
        QMessageBox::warning(this, tr("%1: failed").arg(title), error);
        updateRepositoryInfo();
    });
    // A fetch, pull or push: its progress in the status bar, with a button
    // to stop it, until the queue is done.
    connect(gitCommands_, &GitCommands::progress, this, [this](const QString &title, const QString &line) {
        gitProgress_ = line.isEmpty() ? tr("%1…").arg(title) : tr("%1: %2").arg(title, line);
        statusBar()->showMessage(gitProgress_);
        cancelGitButton_->show();
    });
    connect(gitCommands_, &GitCommands::busyChanged, this, [this](bool busy) {
        if (busy)
            return;
        cancelGitButton_->hide();
        // Failed or cancelled: no success message took its place.
        if (!gitProgress_.isEmpty() && statusBar()->currentMessage() == gitProgress_)
            statusBar()->clearMessage();
        gitProgress_.clear();
    });
    // git and ssh ask for credentials through duet-askpass.
    askpass_ = new AskpassServer([this](const QString &prompt) { return askCredentials(prompt); }, this);
    if (askpass_->listen())
        gitCommands_->setRemoteEnvironment(askpass_->environment());
    gitActions_ = new GitActions(gitCommands_, this, [this] { return folder_; }, this);
    connect(gitActions_, &GitActions::changed, this, &MainWindow::onRepositoryChanged);
    connect(gitActions_, &GitActions::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });
    repositoryWatcher_ = new RepositoryWatcher(this);
    connect(repositoryWatcher_, &RepositoryWatcher::changed, this, &MainWindow::onRepositoryChanged);
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
    projectSettings_ = new ProjectSettingsPopup(toolchains_.get(), theme_, this);
    connect(projectSettings_, &ProjectSettingsPopup::applied, this, &MainWindow::applyProject);
    connect(newBranchPopup_, &NewBranchPopup::createRequested, this, &MainWindow::createBranch);
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
    filesPanel_ = new FilesPanel(this);
    fileTree_ = filesPanel_->tree();
    connect(fileTree_, &FileTreeView::fileActivated, editorTabs_, &EditorTabs::openFile);
    filesPanel_->setAlwaysSelectOpenedFile(QSettings().value(QStringLiteral("alwaysSelectOpenedFile"), false).toBool());
    connect(filesPanel_, &FilesPanel::alwaysSelectOpenedFileChanged, this,
            [](bool enabled) { QSettings().setValue(QStringLiteral("alwaysSelectOpenedFile"), enabled); });
    connect(filesPanel_, &FilesPanel::selectOpenedFileRequested, this, &MainWindow::selectOpenedFile);
    {
        const QSettings settings;
        ProjectTreeModel::Options treeOptions;
        treeOptions.compactFolders = settings.value(QStringLiteral("tree/compactFolders"), true).toBool();
        treeOptions.nestFiles = settings.value(QStringLiteral("tree/nestFiles"), true).toBool();
        treeOptions.showIgnored = settings.value(QStringLiteral("tree/showIgnored"), true).toBool();
        filesPanel_->setTreeOptions(treeOptions);
    }
    connect(filesPanel_, &FilesPanel::treeOptionsChanged, this, [](const ProjectTreeModel::Options &options) {
        QSettings settings;
        settings.setValue(QStringLiteral("tree/compactFolders"), options.compactFolders);
        settings.setValue(QStringLiteral("tree/nestFiles"), options.nestFiles);
        settings.setValue(QStringLiteral("tree/showIgnored"), options.showIgnored);
    });
    connect(fileTree_, &FileTreeView::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });
    connect(fileTree_, &FileTreeView::entryCreated, this, [this] {
        fileStatuses_->refresh();
        symbolIndex_->refresh();
    });
    connect(fileTree_, &FileTreeView::sendToClaudeRequested, this, &MainWindow::sendToClaude);
    connect(fileTree_, &FileTreeView::generateDiagramRequested, this, &MainWindow::generateDiagram);
    connect(fileTree_, &FileTreeView::openTerminalRequested, this,
            [this](const QString &folder) { addTerminal(QFileInfo(folder).fileName(), defaultShell(folder)); });
    connect(fileTree_, &FileTreeView::showDiffRequested, this, [this](const QString &path) {
        setMode(Mode::Diff);
        changesView_->selectFile(QDir(folder_).relativeFilePath(path) == QLatin1String(".")
                                     ? QString()
                                     : QDir(folder_).relativeFilePath(path));
    });
    connect(fileTree_, &FileTreeView::historyRequested, this, [this](const QString &path) {
        const QString relative = QDir(folder_).relativeFilePath(path);
        historyView_->setPathFilter(relative == QLatin1String(".") ? QString() : relative);
        setMode(Mode::History);
    });
    connect(fileTree_, &FileTreeView::stageRequested, this,
            [this](const QString &path) { runGitOnTree(tr("Staging"), path, false); });
    connect(fileTree_, &FileTreeView::rollbackRequested, this,
            [this](const QString &path) { runGitOnTree(tr("Rollback"), path, true); });
    fileStatuses_ = new FileStatusReader(this);
    connect(fileStatuses_, &FileStatusReader::statusesChanged, this,
            [this] { fileTree_->setStatuses(fileStatuses_->statuses(), inRepository_); });
    fileTree_->setUnsavedChangesCheck(
        [this](const QString &path) { return editorTabs_->hasUnsavedChangesUnder(path); });
    connect(fileTree_, &FileTreeView::entryRenamed, this, [this](const QString &oldPath, const QString &newPath) {
        editorTabs_->followRename(oldPath, newPath);
        recentFiles_.rename(oldPath, newPath);
        fileTree_->setClaudeEdited(recentFiles_.editedByClaude());
        fileStatuses_->refresh();
        symbolIndex_->refresh();
    });
    connect(fileTree_, &FileTreeView::entryDeleted, this, [this](const QString &path) {
        editorTabs_->closeFilesUnder(path);
        recentFiles_.remove(path);
        fileTree_->setClaudeEdited(recentFiles_.editedByClaude());
        fileStatuses_->refresh();
        symbolIndex_->refresh();
    });

    claudeConfigView_ = new ClaudeConfigView(this);
    connect(claudeConfigView_, &ClaudeConfigView::fileRequested, this, [this](const QString &path, bool readOnly) {
        editorTabs_->openFile(path);
        if (EditorWidget *editor = editorTabs_->editorFor(path); editor && readOnly)
            editor->setReadOnly(true);
    });
    connect(claudeConfigView_, &ClaudeConfigView::sendToClaudeRequested, this, &MainWindow::sendToClaude);
    claudeConfigView_->setUnsavedChangesCheck(
        [this](const QString &path) { return editorTabs_->hasUnsavedChangesUnder(path); });
    connect(claudeConfigView_, &ClaudeConfigView::entryDeleted, this, [this](const QString &path) {
        editorTabs_->closeFilesUnder(path);
        recentFiles_.remove(path);
        fileTree_->setClaudeEdited(recentFiles_.editedByClaude());
        fileStatuses_->refresh();
        symbolIndex_->refresh();
    });
    connect(claudeConfigView_, &ClaudeConfigView::restartSessionRequested, this, &MainWindow::restartClaudeSession);
    connect(claudeConfigView_, &ClaudeConfigView::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });
    connect(editorTabs_, &EditorTabs::fileSaved, claudeConfigView_, &ClaudeConfigView::scheduleRefresh);
    // Settings, skills and agents checked as they are edited.
    connect(editorTabs_, &EditorTabs::editorOpened, new ClaudeLinter(this), &ClaudeLinter::attach);
    connect(claudeConfigView_, &ClaudeConfigView::effectiveSettingsRequested, this,
            [this] { showEffectiveSettings(true); });
    // An open Effective Settings tab follows the files.
    connect(claudeConfigView_, &ClaudeConfigView::refreshed, this, [this] { showEffectiveSettings(false); });

    outlineView_ = new OutlineView(this);
    problemsView_ = new ProblemsView(problemStore_, this);
    connect(problemsView_, &ProblemsView::problemActivated, this, [this](const QString &path, int line, int column) {
        setMode(Mode::Edit);
        navigator_->goToLine(path, line, std::max(column, 0));
    });
    connect(problemsView_, &ProblemsView::countsChanged, this, &MainWindow::updateTabIcons);
    connect(editorTabs_, &QTabWidget::currentChanged, this,
            [this] { outlineView_->setEditor(editorTabs_->currentEditor()); });
    connect(editorTabs_, &QTabWidget::currentChanged, this, &MainWindow::onCurrentTabChanged);

    // Find bar above the editor tabs, hidden until Ctrl+F.
    findBar_ = new FindBar(this);
    auto *editorArea = new QWidget(this);
    auto *editorLayout = new QVBoxLayout(editorArea);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);
    editorLayout->addWidget(findBar_);
    // Tools of the project's setup not downloaded yet.
    toolsNotice_ = new NoticeBar(editorArea);
    connect(toolsNotice_, &NoticeBar::actionClicked, this, [this] { projectSettings_->download(project_); });
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
        if (editorTabs_->saveAll())
            unsavedEditNotice_->hide();
    });
    editorLayout->addWidget(unsavedEditNotice_);
    editorLayout->addWidget(editorTabs_, 1);

    // Files, Outline and Claude as KDevelop's tool views: vertical tabs at the
    // window's edge, the panel next to them.
    sidebar_ = new SidePanel(this);
    sidebar_->addView(filesPanel_, {}, tr("Files"));
    sidebar_->addView(outlineView_, {}, tr("Outline"));
    sidebar_->addView(claudeConfigView_, {}, tr("Claude"));
    sidebar_->addView(problemsView_, {}, tr("Problems"));

    editSplitter_ = new GripSplitter(Qt::Horizontal, this);
    editSplitter_->setChildrenCollapsible(false);
    editSplitter_->addWidget(sidebar_);
    editSplitter_->addWidget(editorArea);
    editSplitter_->setStretchFactor(1, 1);
    editSplitter_->setSizes({260, 1140});

    auto *page = new QWidget(this);
    auto *pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(sidebar_->strip());
    pageLayout->addWidget(editSplitter_, 1);
    return page;
}

// Terminal mode: the terminal tabs, or a hint as long as there are none.
QWidget *MainWindow::createTerminalPage()
{
    terminalTabs_ = new QTabWidget(this);
    terminalTabs_->setTabsClosable(true);
    terminalTabs_->setDocumentMode(true);
    connect(terminalTabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget *terminal = terminalTabs_->widget(index);
        if (terminal == claudeTerminal_)
            return; // the Claude session stays for the lifetime of the folder
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
    new TrailingTabButton(terminalTabs_, newTerminalButton_); // right after the last tab

    auto *hint = new QLabel(tr("No terminal open\n\n"
                               "Ctrl+Shift+L: start the Claude session"),
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

// Terminal | Edit | Diff | History tabs at the bottom right, in the status bar.
void MainWindow::createModeTabs()
{
    modeTabs_ = new QTabBar(this);
    modeTabs_->setDocumentMode(true);
    modeTabs_->setDrawBase(false);
    modeTabs_->setExpanding(false);
    modeTabs_->setFocusPolicy(Qt::NoFocus);
    modeTabs_->setShape(QTabBar::RoundedSouth);
    // The status bar always gives the tabs their full width: no scroll
    // arrows, no elided titles.
    modeTabs_->setUsesScrollButtons(false);
    modeTabs_->setElideMode(Qt::ElideNone);
    modeTabs_->addTab(tr("Terminal"));
    modeTabs_->addTab(tr("Edit"));
    modeTabs_->addTab(tr("Diff"));
    modeTabs_->addTab(tr("History"));
    modeTabs_->setTabToolTip(0, tr("Full-screen terminals (Ctrl+1)"));
    modeTabs_->setTabToolTip(1, tr("File tree and code editor (Ctrl+2)"));
    modeTabs_->setTabToolTip(2, tr("Pending changes, staging and commit (Ctrl+3)"));
    modeTabs_->setTabToolTip(3, tr("Past commits, their files and their diffs (Ctrl+4)"));
    connect(modeTabs_, &QTabBar::currentChanged, this, [this](int index) { setMode(static_cast<Mode>(index)); });
    createStatusBar();
}

// The bottom bar, built from the right edge leftwards: mode tabs, Sync,
// branch, stop of the git operation, Claude usage, then the editor's
// cursor, line ending, encoding and indentation. On a narrow window the
// widgets furthest left give up their place first, whole; the status
// messages take what is left on the left.
void MainWindow::createStatusBar()
{
    syncButton_ = new QToolButton(this);
    syncButton_->setAutoRaise(true);
    syncButton_->setFocusPolicy(Qt::NoFocus);
    syncButton_->hide();
    connect(syncButton_, &QToolButton::clicked, gitActions_, &GitActions::sync);
    // The branch and how far it is from its upstream; a click opens the
    // Branches popup.
    branchButton_ = new QToolButton(this);
    branchButton_->setObjectName(QStringLiteral("chip"));
    branchButton_->setAutoRaise(true);
    branchButton_->setFocusPolicy(Qt::NoFocus);
    branchButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    branchButton_->hide();
    connect(branchButton_, &QToolButton::clicked, this, &MainWindow::showBranches);
    // Stops the fetch, pull or push that is running.
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

    auto *trailing = new QWidget(this);
    auto *layout = new TrailingLayout(trailing);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    for (QWidget *widget : {static_cast<QWidget *>(modeTabs_), static_cast<QWidget *>(syncButton_),
                            static_cast<QWidget *>(branchButton_), static_cast<QWidget *>(cancelGitButton_),
                            static_cast<QWidget *>(usageLabel_), static_cast<QWidget *>(editorStatus_)})
        layout->addWidget(widget);
    statusBar()->addPermanentWidget(trailing);
}

void MainWindow::createMenus()
{
    QMenu *file = titleBar_->menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New File…"), QKeySequence::New, this, [this] {
        setMode(Mode::Edit);
        fileTree_->createFile();
    });
    file->addAction(tr("New Fol&der…"), this, [this] {
        setMode(Mode::Edit);
        fileTree_->createFolder();
    });
    if (const QList<DiagramKind> kinds = DiagramKinds::available(); !kinds.isEmpty()) {
        QMenu *diagram = file->addMenu(tr("New Dia&gram"));
        for (const DiagramKind &kind : kinds) {
            diagram->addAction(kind.title + QStringLiteral("…"), this, [this, id = kind.id] {
                setMode(Mode::Edit);
                fileTree_->createDiagram(id);
            });
        }
        diagram->addSeparator();
        diagram->addAction(tr("Drawn by &Claude…"), this, [this] { fileTree_->generateDiagram(); });
    }
    file->addAction(tr("Open &Folder…"), QKeySequence(tr("Ctrl+K, Ctrl+O")), this, &MainWindow::chooseFolder);
    file->addAction(tr("&Open File…"), QKeySequence::Open, this, &MainWindow::chooseFile);
    file->addSeparator();
    file->addAction(tr("Project Se&ttings…"), this, &MainWindow::showProjectSettings);
    file->addAction(tr("Close Pro&ject"), this, &MainWindow::closeProject);
    file->addSeparator();
    QAction *preferences =
        file->addAction(tr("&Preferences…"), QKeySequence(tr("Ctrl+,")), this, &MainWindow::showPreferences);
    preferences->setMenuRole(QAction::PreferencesRole);
    file->addSeparator();
    file->addAction(tr("&Save"), QKeySequence::Save, editorTabs_, &EditorTabs::saveCurrent);
    file->addAction(tr("Save &All"), QKeySequence(tr("Ctrl+Alt+S")), editorTabs_, &EditorTabs::saveAll);
    file->addAction(tr("&Close Tab"), QKeySequence::Close, editorTabs_, &EditorTabs::closeCurrent);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    createEditMenu();

    QMenu *claude = titleBar_->menuBar()->addMenu(tr("&Claude"));
    claude->addAction(tr("&Claude Session"), QKeySequence(tr("Ctrl+Shift+L")), this, &MainWindow::showClaudeSession);
    claude->addAction(tr("New &Terminal"), QKeySequence(tr("Ctrl+Shift+T")), this, &MainWindow::newShell);
    if (!DiagramKinds::available().isEmpty())
        claude->addAction(tr("&Draw a Diagram…"), this, [this] { fileTree_->generateDiagram(); });
    claude->addSeparator();
    createCodeRequestActions(claude);
    claude->addSeparator();
    claude->addAction(tr("Restore the State Before a &Prompt…"), this, &MainWindow::showSnapshots);
    const QSettings settings;
    snapshotBeforePrompt_ = claude->addAction(tr("&Snapshot Before Each Prompt"));
    snapshotBeforePrompt_->setCheckable(true);
    snapshotBeforePrompt_->setChecked(settings.value(QStringLiteral("snapshotBeforePrompt"), true).toBool());
    snapshotBeforePrompt_->setToolTip(
        tr("Keep a copy of the working directory before Claude works on a prompt, to undo its changes"));
    connect(snapshotBeforePrompt_, &QAction::toggled, this,
            [](bool checked) { QSettings().setValue(QStringLiteral("snapshotBeforePrompt"), checked); });
    shareEditorContext_ = claude->addAction(tr("Share &Editor Context With Each Prompt"));
    shareEditorContext_->setCheckable(true);
    shareEditorContext_->setChecked(settings.value(QStringLiteral("shareEditorContext"), true).toBool());
    shareEditorContext_->setToolTip(
        tr("Tell Claude the open file, the cursor, the selection, the problems shown and the unsaved files"));
    connect(shareEditorContext_, &QAction::toggled, this,
            [](bool checked) { QSettings().setValue(QStringLiteral("shareEditorContext"), checked); });
    exposeTools_ = claude->addAction(tr("E&xpose Duet's Tools to Claude"));
    exposeTools_->setCheckable(true);
    exposeTools_->setChecked(settings.value(QStringLiteral("exposeTools"), true).toBool());
    exposeTools_->setToolTip(tr("Let Claude read and act in the editor through Duet's MCP server (next session)"));
    connect(exposeTools_, &QAction::toggled, this,
            [](bool checked) { QSettings().setValue(QStringLiteral("exposeTools"), checked); });
    claudeNotifications_ = claude->addAction(tr("&Notifications"));
    claudeNotifications_->setCheckable(true);
    claudeNotifications_->setChecked(settings.value(QStringLiteral("claudeNotifications"), true).toBool());
    claudeNotifications_->setToolTip(tr("Alert when Claude needs you or has finished while you are elsewhere"));
    connect(claudeNotifications_, &QAction::toggled, this,
            [](bool checked) { QSettings().setValue(QStringLiteral("claudeNotifications"), checked); });

    createNavigateMenu();
    createRunMenu();
    createGitMenu();

    QMenu *view = titleBar_->menuBar()->addMenu(tr("&View"));
    paletteAction_ = view->addAction(tr("&Command Palette…"), this, &MainWindow::showCommandPalette);
    paletteAction_->setShortcut(QKeySequence(tr("Ctrl+Shift+P")));
    paletteAction_->setShortcutContext(Qt::ApplicationShortcut);
    view->addSeparator();
    QMenu *themes = view->addMenu(tr("T&heme"));
    auto *themeGroup = new QActionGroup(themes);
    themeActions_ = themeGroup;
    for (const Theme &theme : Theme::all()) {
        QAction *action = themes->addAction(theme.title);
        action->setCheckable(true);
        action->setChecked(theme.name == savedThemeName());
        action->setData(theme.name);
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, name = theme.name] { setTheme(name); });
    }
    view->addSeparator();

    // Ctrl+1/2/3/4 remain active from a terminal (see TerminalWidget::event).
    modeActions_ = new QActionGroup(this);
    const QStringList modeNames{tr("&Terminal Mode"), tr("&Edit Mode"), tr("&Diff Mode"), tr("&History Mode")};
    for (int i = 0; i < modeNames.size(); ++i) {
        QAction *action = view->addAction(modeNames[i]);
        action->setCheckable(true);
        action->setShortcut(QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + i)));
        action->setShortcutContext(Qt::ApplicationShortcut);
        modeActions_->addAction(action);
        connect(action, &QAction::triggered, this, [this, i] { setMode(static_cast<Mode>(i)); });
    }
    modeActions_->actions().at(static_cast<int>(mode_))->setChecked(true);
    QAction *toggleTerminal = view->addAction(tr("Switch to Terminal"));
    toggleTerminal->setShortcut(QKeySequence(Qt::Key_F11));
    toggleTerminal->setShortcutContext(Qt::ApplicationShortcut);
    connect(toggleTerminal, &QAction::triggered, this, &MainWindow::toggleTerminalMode);
    view->addSeparator();
    view->addAction(tr("File &Outline"), QKeySequence(tr("Ctrl+Shift+O")), this, &MainWindow::showOutline);
    view->addAction(tr("&Problems"), QKeySequence(tr("Alt+6")), this, [this] {
        setMode(Mode::Edit);
        sidebar_->showView(problemsView_);
        problemsView_->tree()->setFocus();
    });
    view->addAction(tr("&Claude Configuration"), this, [this] {
        setMode(Mode::Edit);
        sidebar_->showView(claudeConfigView_);
        claudeConfigView_->tree()->setFocus();
    });

    // Also folded by clicking its current tab.
    showSidebar_ = view->addAction(tr("&Side Panel"));
    showSidebar_->setCheckable(true);
    showSidebar_->setChecked(true);
    connect(showSidebar_, &QAction::toggled, sidebar_, &SidePanel::setPanelVisible);
    connect(sidebar_, &SidePanel::panelVisibilityChanged, showSidebar_, &QAction::setChecked);
    showSidebar_->setChecked(settings.value(QStringLiteral("showSidebar"), true).toBool());

    view->addSeparator();
    systemTitleBar_ = view->addAction(tr("Use System &Title Bar"));
    systemTitleBar_->setCheckable(true);
    systemTitleBar_->setToolTip(tr("The system's window frame instead of Duet's (tiling window managers…)"));
    connect(systemTitleBar_, &QAction::toggled, this, [this](bool system) {
        setSystemTitleBar(system);
        QSettings().setValue(QStringLiteral("systemTitleBar"), system);
    });
#ifdef Q_OS_MACOS
    systemTitleBar_->setVisible(false); // macOS keeps its own frame and traffic lights
#endif

    createHelpMenu();

    // Actions at the right of the title bar.
    titleBar_->setRunActions(runAction_, stopAction_);
    titleBar_->addActionButton(goToFileAction_, QStringLiteral("search"));
    titleBar_->addActionButton(findInFilesAction_, QStringLiteral("find-in-files"));
    titleBar_->addActionButton(paletteAction_, QStringLiteral("palette"));
}

void MainWindow::createEditMenu()
{
    QMenu *edit = titleBar_->menuBar()->addMenu(tr("&Edit"));
    edit->addAction(tr("&Find…"), QKeySequence::Find, this, [this] { showFind(false); });
    edit->addAction(tr("&Replace…"), QKeySequence(tr("Ctrl+R")), this, [this] { showFind(true); });
    edit->addAction(tr("Find &Next"), QKeySequence(Qt::Key_F3), this, [this] {
        if (mode_ == Mode::Edit)
            findBar_->findNext();
    });
    edit->addAction(tr("Find &Previous"), QKeySequence(Qt::SHIFT | Qt::Key_F3), this, [this] {
        if (mode_ == Mode::Edit)
            findBar_->findPrevious();
    });
    edit->addSeparator();
    edit->addAction(tr("&Complete Code"), QKeySequence(tr("Ctrl+Space")), this, [this] {
        if (CompletionController *completion = CompletionController::of(editorInEditMode()))
            completion->complete();
    });
    edit->addAction(tr("S&mart Complete Code"), QKeySequence(tr("Ctrl+Shift+Space")), this, [this] {
        if (CompletionController *completion = CompletionController::of(editorInEditMode()))
            completion->completeSmart();
    });
    const auto inCode = [this](void (CodeIntelligence::*command)()) {
        return [this, command] {
            if (mode_ == Mode::Edit)
                (intelligence_->*command)();
        };
    };
    edit->addAction(tr("&Parameter Info"), QKeySequence(tr("Ctrl+P")), this,
                    inCode(&CodeIntelligence::showParameterInfo));
    edit->addAction(tr("&Quick Documentation"), QKeySequence(tr("Ctrl+Q")), this,
                    inCode(&CodeIntelligence::showDocumentation));
    edit->addAction(tr("Show Quick F&ixes…"), QKeySequence(tr("Alt+Return")), this,
                    inCode(&CodeIntelligence::showQuickFixes));
    edit->addAction(tr("Re&name…"), QKeySequence(tr("Shift+F6")), this, inCode(&CodeIntelligence::rename));
    edit->addAction(tr("F&ormat Document"), QKeySequence(tr("Ctrl+Alt+L")), this, [this] {
        if (mode_ == Mode::Edit)
            editorTabs_->formatCurrent();
    });
    formatOnSave_ = edit->addAction(tr("Format on Sa&ve"));
    formatOnSave_->setCheckable(true);
    formatOnSave_->setChecked(QSettings().value(QStringLiteral("formatOnSave"), true).toBool());
    formatOnSave_->setToolTip(tr("Format each file with its language's formatter before saving it"));
    editorTabs_->setFormatOnSave(formatOnSave_->isChecked());
    connect(formatOnSave_, &QAction::toggled, this, [this](bool checked) {
        editorTabs_->setFormatOnSave(checked);
        QSettings().setValue(QStringLiteral("formatOnSave"), checked);
    });
    edit->addSeparator();
    createFoldingMenu(edit);
    edit->addSeparator();
    findInFilesAction_ =
        edit->addAction(tr("Find in F&iles…"), QKeySequence(tr("Ctrl+Shift+F")), this, &MainWindow::showFindInFiles);
}

void MainWindow::createFoldingMenu(QMenu *edit)
{
    const auto inEditor = [this](void (EditorWidget::*fold)(bool), bool folds) {
        return [this, fold, folds] {
            if (EditorWidget *editor = editorInEditMode())
                (editor->*fold)(folds);
        };
    };
    QMenu *folding = edit->addMenu(tr("Fol&ding"));
    folding->addAction(tr("&Fold Block"), QKeySequence(tr("Ctrl+-")), this, inEditor(&EditorWidget::foldBlock, true));
    folding->addAction(tr("&Unfold Block"), this, inEditor(&EditorWidget::foldBlock, false))
        ->setShortcuts({QKeySequence(tr("Ctrl+=")), QKeySequence(tr("Ctrl++"))});
    folding->addAction(tr("Fold &All"), QKeySequence(tr("Ctrl+Shift+-")), this,
                       inEditor(&EditorWidget::foldEverything, true));
    folding->addAction(tr("Unfold A&ll"), this, inEditor(&EditorWidget::foldEverything, false))
        ->setShortcuts({QKeySequence(tr("Ctrl+Shift+=")), QKeySequence(tr("Ctrl+Shift++"))});
}

EditorWidget *MainWindow::editorInEditMode() const
{
    return mode_ == Mode::Edit ? editorTabs_->currentEditor() : nullptr;
}

void MainWindow::createNavigateMenu()
{
    QMenu *navigate = titleBar_->menuBar()->addMenu(tr("&Navigate"));
    goToFileAction_ = navigate->addAction(tr("Go to &File…"), QKeySequence(tr("Ctrl+Shift+N")), this,
                                          [this] { showGoTo(static_cast<int>(GoToPopup::Tab::Files)); });
    navigate->addAction(tr("Go to &Symbol…"), QKeySequence(tr("Ctrl+Alt+Shift+N")), this,
                        [this] { showGoTo(static_cast<int>(GoToPopup::Tab::Symbols)); });
    navigate->addAction(tr("&Recent Files…"), QKeySequence(tr("Ctrl+E")), this,
                        [this] { showGoTo(static_cast<int>(GoToPopup::Tab::Recent)); });
    navigate->addAction(tr("Select Opened File in &Tree"), QKeySequence(tr("Alt+F1")), this,
                        &MainWindow::selectOpenedFile);
    navigate->addAction(tr("Go to &Line:Column…"), QKeySequence(tr("Ctrl+G")), this, [this] {
        if (mode_ == Mode::Edit && editorTabs_->currentEditor()) {
            editorTabs_->revealEditor(editorTabs_->currentEditor());
            editorStatus_->goToLine();
        }
    });
    navigate->addSeparator();
    navigate->addAction(tr("Go to &Definition"), QKeySequence(tr("Ctrl+B")), this, [this] {
        if (mode_ == Mode::Edit)
            intelligence_->goToDefinition();
    });
    navigate->addAction(tr("Find &Usages"), QKeySequence(tr("Alt+F7")), this, [this] {
        if (mode_ == Mode::Edit)
            intelligence_->findUsages();
    });
    navigate->addAction(tr("Switch &Header/Source"), QKeySequence(tr("Ctrl+Alt+Home")), this, [this] {
        if (mode_ == Mode::Edit)
            intelligence_->switchHeaderSource();
    });
    navigate->addAction(tr("&Back"), QKeySequence(tr("Alt+Left")), navigator_, &CodeNavigator::goBack);
    navigate->addAction(tr("&Forward"), QKeySequence(tr("Alt+Right")), navigator_, &CodeNavigator::goForward);
    navigate->addSeparator();
    navigate->addAction(tr("&Next Change"), QKeySequence(tr("Ctrl+Alt+Shift+Down")), this,
                        [this] { goToChange(true); });
    navigate->addAction(tr("&Previous Change"), QKeySequence(tr("Ctrl+Alt+Shift+Up")), this,
                        [this] { goToChange(false); });
    navigate->addAction(tr("R&ollback Lines"), QKeySequence(tr("Ctrl+Alt+Z")), this, &MainWindow::rollbackChange);
}

void MainWindow::createHelpMenu()
{
    QMenu *help = titleBar_->menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Check &Prerequisites…"), this, &MainWindow::showPrerequisites);
}

void MainWindow::createRunMenu()
{
    QMenu *run = titleBar_->menuBar()->addMenu(tr("&Run"));
    runAction_ = run->addAction(tr("&Run"), QKeySequence(tr("Shift+F10")), this, &MainWindow::runSelected);
    QMenu *runOther = run->addMenu(tr("Run &Configuration"));
    connect(runOther, &QMenu::aboutToShow, this, [this, runOther] { fillRunMenu(runOther, true); });
    run->addAction(tr("Run &Other…"), QKeySequence(tr("Alt+Shift+F10")), this, [this] {
        // The configurations under the title bar's chip.
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        fillRunMenu(menu, true);
        menu->popup(titleBar_->mapToGlobal(
            QPoint((titleBar_->width() / 2) - (menu->sizeHint().width() / 2), titleBar_->height())));
    });
    buildAction_ = run->addAction(tr("&Build Project"), QKeySequence(tr("Ctrl+F9")), this, &MainWindow::buildProject);
    stopAction_ = run->addAction(tr("&Stop"), QKeySequence(tr("Ctrl+F2")), this, &MainWindow::stopRun);
    run->addSeparator();
    run->addAction(tr("&Edit Configurations…"), this, &MainWindow::editRunConfigurations);
    run->addSeparator();
    run->addAction(tr("&Next Problem"), QKeySequence(Qt::Key_F2), this, [this] { goToProblem(true); });
    run->addAction(tr("&Previous Problem"), QKeySequence(tr("Shift+F2")), this, [this] { goToProblem(false); });

    runChooser_ = new QMenu(this);
    connect(runChooser_, &QMenu::aboutToShow, this, [this] { fillRunMenu(runChooser_, false); });
    // The configurations follow the project's files.
    connect(editorTabs_, &EditorTabs::fileSaved, this, [this](const QString &path) {
        if (QFileInfo(path) != QFileInfo(RunConfigurations::filePath(folder_)))
            return;
        QString error;
        runner_->configurations(&error);
        if (!error.isEmpty())
            statusBar()->showMessage(error, statusMessageMs);
        updateRunWidgets();
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive)
            updateRunWidgets();
    });
}

void MainWindow::fillRunMenu(QMenu *menu, bool run)
{
    menu->clear();
    QString error;
    const QList<RunConfiguration> configurations = runner_->configurations(&error);
    const std::optional<RunConfiguration> selected = runner_->selected();
    // .duet/run.json's first, then the detected ones by kind.
    const auto heading = [](const RunConfiguration &configuration) {
        if (!configuration.detected)
            return tr("Project");
        switch (configuration.kind) {
        case RunConfiguration::Kind::Build:
            return tr("Build");
        case RunConfiguration::Kind::Test:
            return tr("Test");
        case RunConfiguration::Kind::Run:
            break;
        }
        return tr("Run");
    };
    QString previous;
    for (const RunConfiguration &configuration : configurations) {
        if (heading(configuration) != previous) {
            previous = heading(configuration);
            RunConfigurationMenu::addHeading(menu, previous, theme_);
        }
        const QAction *action = RunConfigurationMenu::addConfiguration(menu, configuration,
                                                                       selected && selected->name == configuration.name,
                                                                       runner_->isRunning(configuration.name), theme_);
        connect(action, &QAction::triggered, this, [this, configuration, run] {
            runner_->select(configuration.name);
            if (run)
                runConfiguration(configuration);
        });
    }
    if (configurations.isEmpty())
        RunConfigurationMenu::addNote(menu, tr("No configuration found in the project"), theme_);
    if (!error.isEmpty())
        RunConfigurationMenu::addNote(menu, error, theme_);
    menu->addSeparator();
    menu->addAction(UiIcons::icon(QStringLiteral("settings"), theme_.foreground), tr("&Edit Configurations…"), this,
                    &MainWindow::editRunConfigurations);
}

void MainWindow::updateRunWidgets()
{
    if (!runner_ || !runAction_)
        return;
    const std::optional<RunConfiguration> selected = runner_->selected();
    const bool any = !runner_->running().isEmpty();
    stopAction_->setEnabled(any);
    runAction_->setEnabled(selected.has_value());
    buildAction_->setEnabled(runner_->buildConfiguration().has_value());
    if (!selected) {
        runAction_->setText(tr("&Run"));
        stopAction_->setText(tr("&Stop"));
        titleBar_->setRunConfiguration(UiIcons::icon(QStringLiteral("run"), theme_.mutedText), tr("Add Configuration"),
                                       tr("No build file found: write the project's commands in .duet/run.json"),
                                       runChooser_, false);
        return;
    }
    const bool running = runner_->isRunning(selected->name);
    runAction_->setText(running ? tr("&Rerun “%1”").arg(selected->name) : tr("&Run “%1”").arg(selected->name));
    // stopRun() ends the selected configuration first.
    stopAction_->setText(running ? tr("&Stop “%1”").arg(selected->name) : tr("&Stop"));
    QString toolTip = selected->command;
    if (!selected->folder.isEmpty())
        toolTip += tr("\nin %1").arg(QDir::toNativeSeparators(selected->folder));
    if (running)
        toolTip += tr("\nRunning");
    titleBar_->setRunConfiguration(
        UiIcons::icon(runKindIconName(selected->kind), running ? QColor("#4fa36b") : theme_.foreground), selected->name,
        toolTip, runChooser_, running);
}

void MainWindow::runSelected()
{
    if (const std::optional<RunConfiguration> configuration = runner_->selected())
        runConfiguration(*configuration);
    else
        editRunConfigurations();
}

void MainWindow::buildProject()
{
    if (const std::optional<RunConfiguration> configuration = runner_->buildConfiguration())
        runConfiguration(*configuration);
    else
        statusBar()->showMessage(tr("No build configuration: add one in .duet/run.json"), statusMessageMs);
}

void MainWindow::runConfiguration(const RunConfiguration &configuration)
{
    // What runs is what the editor shows.
    editorTabs_->saveAll();
    TerminalWidget *previous = runner_->terminal(configuration.name);
    if (previous && previous->isRunning())
        previous->terminate();

    auto *terminal = new TerminalWidget(terminalTabs_);
    terminal->applyTheme(theme_);
    int index = previous ? terminalTabs_->indexOf(previous) : -1;
    if (index >= 0) {
        terminalTabs_->removeTab(index);
        previous->deleteLater();
        index = terminalTabs_->insertTab(index, terminal, configuration.name);
    } else {
        index = terminalTabs_->addTab(terminal, configuration.name);
    }
    terminalTabs_->setTabToolTip(index, configuration.command);
    setRunTabIcon(terminal, runKindIconName(configuration.kind), {});
    // Closing the tab ends the run.
    connect(terminal, &QObject::destroyed, this,
            [this] { QTimer::singleShot(0, this, &MainWindow::updateRunWidgets); });

    // The run's terminal comes to the front, whatever its kind.
    terminalTabs_->setCurrentIndex(index);
    updateTerminalPage();
    setMode(Mode::Terminal);
    terminal->setFocus();
    runner_->start(configuration, terminal);
}

void MainWindow::setRunTabIcon(TerminalWidget *terminal, const QString &iconName, const QColor &color)
{
    terminal->setProperty("runIcon", iconName);
    terminal->setProperty("runColor", color);
    if (const int index = terminalTabs_->indexOf(terminal); index >= 0)
        terminalTabs_->setTabIcon(index, UiIcons::icon(iconName, color.isValid() ? color : theme_.mutedText));
}

void MainWindow::onRunFinished(const RunConfiguration &configuration, int exitCode, int errors, int warnings)
{
    TerminalWidget *terminal = runner_->terminal(configuration.name);
    if (terminal)
        setRunTabIcon(terminal, runKindIconName(configuration.kind), runOutcomeColor(exitCode));
    statusBar()->showMessage(runFinishedMessage(configuration, exitCode, errors, warnings), statusMessageMs);

    const bool failed = exitCode != 0;
    if (exitCode != -1 && configuration.kind != RunConfiguration::Kind::Run) {
        // A build or tests that went on out of sight.
        const bool hasProblemsToShow = errors > 0 || (failed && warnings > 0);
        if (mode_ == Mode::Edit && hasProblemsToShow)
            sidebar_->showView(problemsView_);
        else if (failed && terminal && mode_ != Mode::Terminal)
            terminalTabs_->setCurrentWidget(terminal); // F11 shows it
        if (!isActiveWindow())
            QApplication::alert(this);
    }
    updateRunWidgets();
}

void MainWindow::stopRun()
{
    const QStringList running = runner_->running();
    if (running.isEmpty())
        return;
    QString name = running.first();
    const std::optional<RunConfiguration> selected = runner_->selected();
    if (selected && running.contains(selected->name))
        name = selected->name;
    if (mode_ == Mode::Terminal) {
        for (const QString &candidate : running) {
            if (runner_->terminal(candidate) == terminalTabs_->currentWidget())
                name = candidate;
        }
    }
    runner_->stop(name);
    statusBar()->showMessage(tr("Stopping “%1”… (Stop again to end it at once)").arg(name), statusMessageMs);
}

void MainWindow::editRunConfigurations()
{
    const QString path = RunConfigurations::filePath(folder_);
    if (!QFileInfo::exists(path)) {
        // Started from the chosen configuration, which it then replaces.
        RunConfiguration example;
        if (const std::optional<RunConfiguration> selected = runner_->selected()) {
            example = *selected;
        } else {
            example.name = tr("Run");
            example.command = QStringLiteral("echo Hello");
        }
        example.detected = false;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, tr("Edit Configurations"),
                                 tr("Cannot create %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
            return;
        }
        file.write(RunConfigurations::serialize({example}));
        file.close();
        fileStatuses_->refresh();
        statusBar()->showMessage(tr("A configuration of .duet/run.json replaces the detected one of the same name"),
                                 statusMessageMs);
    }
    setMode(Mode::Edit);
    editorTabs_->openFile(path);
}

void MainWindow::goToProblem(bool forward)
{
    EditorWidget *editor = editorInEditMode();
    if (!editor || editor->diagnostics().isEmpty()) {
        statusBar()->showMessage(tr("No problems in this file"), statusMessageMs);
        return;
    }
    Diagnostics problems = editor->diagnostics();
    std::ranges::sort(problems, {}, &Diagnostic::start);
    const auto cursor = static_cast<int>(editor->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS));
    const Diagnostic &target = forward ? problemAfter(problems, cursor) : problemBefore(problems, cursor);
    int line = 0;
    int index = 0;
    editor->lineIndexFromPosition(target.start, &line, &index);
    editor->setCursorPosition(line, index);
    editor->ensureLineVisible(line);
    editor->setFocus();
    statusBar()->showMessage(target.message, statusMessageMs);
}

void MainWindow::createGitMenu()
{
    QMenu *git = titleBar_->menuBar()->addMenu(tr("&Git"));
    git->addAction(tr("&Prepare Commit"), QKeySequence(tr("Ctrl+Shift+G")), this, [this] {
        setMode(Mode::Diff);
        changesView_->focusCommitMessage();
    });
    git->addAction(tr("&Generate Commit Message"), QKeySequence(tr("Ctrl+Alt+G")), this, [this] {
        setMode(Mode::Diff);
        changesView_->generateCommitMessage();
    });
    git->addAction(tr("&Stage All"), this, [this] {
        setMode(Mode::Diff);
        changesView_->stageAll();
    });
    git->addAction(tr("&Unstage All"), this, [this] {
        setMode(Mode::Diff);
        changesView_->unstageAll();
    });
    git->addSeparator();
    git->addAction(tr("&Fetch"), gitActions_, &GitActions::fetch);
    git->addAction(tr("P&ull"), QKeySequence(tr("Ctrl+Alt+U")), gitActions_, &GitActions::pull);
    git->addAction(tr("Pu&sh"), QKeySequence(tr("Ctrl+Alt+P")), gitActions_, &GitActions::push);
    git->addAction(tr("S&ync (Pull, then Push)"), gitActions_, &GitActions::sync);
    git->addSeparator();
    git->addAction(tr("&Branches…"), QKeySequence(tr("Ctrl+Alt+B")), this, &MainWindow::showBranches);
    git->addAction(tr("&New Branch…"), this, &MainWindow::showNewBranch);
    git->addSeparator();
    continueAction_ = git->addAction(tr("&Continue Merge or Rebase"), gitActions_, &GitActions::continueOperation);
    skipAction_ = git->addAction(tr("S&kip Commit"), gitActions_, &GitActions::skipCommit);
    abortAction_ = git->addAction(tr("&Abort Merge or Rebase…"), gitActions_, &GitActions::abortOperation);
    for (QAction *action : {continueAction_, skipAction_, abortAction_})
        action->setEnabled(false);
    git->addSeparator();
    git->addAction(tr("S&tash Changes…"), gitActions_, &GitActions::stashChanges);
    git->addAction(tr("Stas&hes…"), this, &MainWindow::showStashes);
    git->addSeparator();
    git->addAction(tr("Restore a S&napshot…"), this, &MainWindow::showSnapshots);
}

void MainWindow::restoreSettings()
{
    const QSettings settings;
    // Before the window first shows: the frame changes the native window.
    {
        const bool system = settings.value(QStringLiteral("systemTitleBar"), false).toBool();
        const QSignalBlocker blocker(systemTitleBar_);
        systemTitleBar_->setChecked(system);
        setSystemTitleBar(system);
    }
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    editSplitter_->restoreState(settings.value(QStringLiteral("editSplitter")).toByteArray());
    changesView_->splitter()->restoreState(settings.value(QStringLiteral("diffSplitter")).toByteArray());
    historyView_->restoreState(settings.value(QStringLiteral("historySplitter")).toByteArray());
    sidebar_->tabBar()->setCurrentIndex(settings.value(QStringLiteral("sidebarTab"), 0).toInt());
    sidebar_->setPanelVisible(showSidebar_->isChecked());
    setTheme(savedThemeName());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSession(); // before closeAll, which empties the tabs
    if (!editorTabs_->closeAll()) {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("editSplitter"), editSplitter_->saveState());
    settings.setValue(QStringLiteral("diffSplitter"), changesView_->splitter()->saveState());
    settings.setValue(QStringLiteral("historySplitter"), historyView_->saveState());
    settings.setValue(QStringLiteral("showSidebar"), sidebar_->isPanelVisible());
    settings.setValue(QStringLiteral("sidebarTab"), sidebar_->tabBar()->currentIndex());
    settings.setValue(QStringLiteral("viewMode"), static_cast<int>(mode_));
    settings.setValue(QStringLiteral("lastFolder"), folder_);
    // The welcome window shows before this one closes: the application
    // goes on.
    if (closingProject_)
        emit projectClosed();
    event->accept();
}

void MainWindow::openFolder(const QString &folder)
{
    if (!folder_.isEmpty()) {
        saveSession();
        if (!editorTabs_->closeAll())
            return; // unsaved changes: the user cancelled
    }
    const QString previousFolder = folder_;
    folder_ = QDir(folder).absolutePath();
    fileTree_->setRootPath(folder_);
    claudeConfigView_->setProjectFolder(folder_);
    claudeTouched_.clear();
    updateClaudeMarks();
    symbolIndex_->setFolder(folder_);
    completion_->history()->setFile(CompletionHistory::fileFor(folder_)); // the project's choices
    project_ = ProjectStore::touch(folder_);
    if (!project_.configured) {
        // Opened without the welcome window: set up from its files.
        const QDateTime lastOpened = project_.lastOpened;
        project_ = ProjectSetupForm::detectedProject(folder_, *toolchains_);
        project_.lastOpened = lastOpened;
        project_.configured = true;
        ProjectStore::save(project_);
    }
    languageServers_->setProject(project_, toolchains_.get());
    warmUpLanguageServers();
    if (preferencesPopup_)
        preferencesPopup_->setInstalledServers(languageServers_->installed());
    updateToolsNotice();
    problemStore_->clearBuildProblems();
    problemsView_->setProjectFolder(folder_);
    runner_->setProject(project_, toolchains_.get());
    goToPopup_->setFolder(folder_);
    fileStatuses_->setFolder(folder_);
    findInFiles_->setFolder(folder_);
    restoreSession();
    changesView_->setFolder(folder_);
    historyView_->setFolder(folder_);
    updateRepositoryInfo();
    if (folder_ != previousFolder || !claudeTerminal_)
        startClaudeSession();
    if (mode_ == Mode::Diff)
        changesView_->refresh();
    if (mode_ == Mode::History)
        historyView_->refresh();
    setWindowTitle(QStringLiteral("%1 — Duet").arg(QFileInfo(folder_).fileName()));
    updateProjectMenu();
}

void MainWindow::saveSession()
{
    QSettings settings;
    QVariantMap sessions = settings.value(QStringLiteral("sessions")).toMap();
    sessions.insert(folder_, QVariantMap{
                                 {QStringLiteral("openFiles"), editorTabs_->openFilePaths()},
                                 {QStringLiteral("currentFile"), editorTabs_->currentFilePath()},
                                 {QStringLiteral("expandedPaths"), fileTree_->expandedPaths()},
                                 {QStringLiteral("recentFiles"), recentFiles_.opened()},
                                 {QStringLiteral("editedByClaude"), recentFiles_.editedByClaude()},
                             });
    settings.setValue(QStringLiteral("sessions"), sessions);
}

void MainWindow::restoreSession()
{
    const QVariantMap session = QSettings().value(QStringLiteral("sessions")).toMap().value(folder_).toMap();
    recentFiles_.clear();
    if (session.isEmpty())
        return;
    fileTree_->restoreExpandedPaths(session.value(QStringLiteral("expandedPaths")).toStringList());
    for (const QString &path : session.value(QStringLiteral("openFiles")).toStringList()) {
        if (QFileInfo(path).isFile())
            editorTabs_->openFile(path);
    }
    const QString currentFile = session.value(QStringLiteral("currentFile")).toString();
    if (QFileInfo(currentFile).isFile())
        editorTabs_->openFile(currentFile); // already open: makes it current
    // After the files are open: opening them changes the order.
    recentFiles_.restore(session.value(QStringLiteral("recentFiles")).toStringList(),
                         session.value(QStringLiteral("editedByClaude")).toStringList());
    fileTree_->setClaudeEdited(recentFiles_.editedByClaude());
}

void MainWindow::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open Folder"), folder_);
    if (folder.isEmpty())
        return;
    const std::optional<ProjectConfig> known = ProjectStore::find(folder);
    const bool imported = !known || !known->configured;
    openFolder(folder);
    // A new project: its detected setup, to check.
    if (imported && folder_ == QDir(folder).absolutePath())
        showProjectSettings();
}

void MainWindow::showProjectSettings()
{
    projectSettings_->popup(project_);
}

void MainWindow::applyProject(const ProjectConfig &project)
{
    if (QDir(project.path).absolutePath() != folder_)
        return; // another project's downloads, finished after a switch
    project_ = project;
    languageServers_->setProject(project_, toolchains_.get());
    runner_->setProject(project_, toolchains_.get());
    warmUpLanguageServers();
    if (preferencesPopup_)
        preferencesPopup_->setInstalledServers(languageServers_->installed());
    dismissedServerNotices_.clear();
    updateToolsNotice();
    updateServerNotice();
}

void MainWindow::closeProject()
{
    closingProject_ = true;
    if (!close())
        closingProject_ = false;
}

void MainWindow::updateToolsNotice()
{
    const QList<InstallStep> steps = InstallPlan::plan(project_, *toolchains_);
    if (steps.isEmpty()) {
        toolsNotice_->hide();
        return;
    }
    toolsNotice_->showNotice(
        tr("This project's setup needs tools Duet has not downloaded yet: %1").arg(InstallPlan::summary(steps)),
        tr("Download"));
}

void MainWindow::chooseFile()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Open Files"), folder_);
    for (const QString &file : files)
        editorTabs_->openFile(file);
}

// The Claude session lives in the first terminal tab, which cannot be
// closed. Opening a folder starts it there, replacing the previous one.
bool MainWindow::startClaudeSession(QString *error, const QStringList &extraArguments)
{
    if (claudeTerminal_) {
        terminalTabs_->removeTab(terminalTabs_->indexOf(claudeTerminal_));
        claudeTerminal_->deleteLater();
        claudeTerminal_ = nullptr;
    }
    QString launchError;
    const std::optional<ProcessSpec> spec = ClaudeLauncher::claudeProcess(
        folder_, hookServer_.serverName(), &launchError, extraArguments,
        exposeTools_->isChecked() ? std::optional(mcpRegistry_.readOnlyToolPermissions(QStringLiteral("duet")))
                                  : std::nullopt);
    if (!spec) {
        statusBar()->showMessage(tr("Claude session unavailable: %1").arg(launchError));
        if (error)
            *error = std::move(launchError);
        updateTerminalPage();
        return false;
    }
    claudeTerminal_ = new TerminalWidget(terminalTabs_);
    claudeTerminal_->applyTheme(theme_);
    claudeTerminal_->installEventFilter(this); // Escape interrupts the response, Ctrl+C too
    connect(claudeTerminal_, &TerminalWidget::finished, &claudeActivity_, &ClaudeActivity::reset);
    claudeActivity_.reset();
    claudeStatus_ = {};
    updateUsage();
    terminalTabs_->insertTab(0, claudeTerminal_, QStringLiteral("Claude"));
    const auto closeSide = static_cast<QTabBar::ButtonPosition>(
        terminalTabs_->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, terminalTabs_->tabBar()));
    terminalTabs_->tabBar()->setTabButton(0, closeSide, nullptr);
    terminalTabs_->setCurrentIndex(0);
    updateTerminalPage();
    claudeTerminal_->start(*spec);
    if (mode_ == Mode::Terminal)
        claudeTerminal_->setFocus();
    updateClaudeIndicator();
    updateClaudeWatched();
    return true;
}

// Ctrl+Shift+L: back to the Claude session, restarted if it has exited.
void MainWindow::showClaudeSession()
{
    if (!claudeTerminal_ || !claudeTerminal_->isRunning()) {
        QString error;
        if (!startClaudeSession(&error)) {
            QMessageBox::warning(this, tr("Claude Session"), error);
            return;
        }
    }
    terminalTabs_->setCurrentWidget(claudeTerminal_);
    setMode(Mode::Terminal);
}

// Claude reads part of its configuration only when it starts: a new
// session, which goes on with the last conversation.
void MainWindow::restartClaudeSession()
{
    if (claudeActivity_.state() == ClaudeActivity::State::Working &&
        QMessageBox::question(this, tr("Restart Claude Session"),
                              tr("Claude is working. Stop it and restart the session?")) != QMessageBox::Yes)
        return;
    QString error;
    if (!startClaudeSession(&error, {QStringLiteral("--continue")})) {
        QMessageBox::warning(this, tr("Claude Session"), error);
        return;
    }
    setMode(Mode::Terminal);
}

void MainWindow::showEffectiveSettings(bool open)
{
    const QString title = tr("Effective Settings");
    if (!open && !editorTabs_->textView(title))
        return;
    QWidget *current = editorTabs_->currentWidget();
    const QList<EffectiveSettings::Layer> layers =
        EffectiveSettings::read(folder_, {}); // without Duet's hooks: not for the user to see
    editorTabs_->showText(title, EffectiveSettings::render(layers, folder_), QStringLiteral("json"),
                          QStringLiteral("settings"));
    if (open)
        setMode(Mode::Edit);
    else
        editorTabs_->setCurrentWidget(current); // updated in the background
}

void MainWindow::newShell()
{
    addTerminal(tr("Terminal"), defaultShell(folder_));
}

TerminalWidget *MainWindow::addTerminal(const QString &title, const ProcessSpec &spec)
{
    auto *terminal = new TerminalWidget(terminalTabs_);
    terminal->applyTheme(theme_);
    const int index = terminalTabs_->addTab(terminal, title);
    terminalTabs_->setTabIcon(index, UiIcons::icon(QStringLiteral("terminal"), theme_.mutedText));
    terminalTabs_->setCurrentIndex(index);
    updateTerminalPage();
    setMode(Mode::Terminal);
    terminal->start(spec);
    terminal->setFocus();
    return terminal;
}

QString MainWindow::savedThemeName()
{
    return Preferences::load().theme;
}

void MainWindow::setTheme(const QString &name)
{
    theme_ = Theme::named(name);
    Preferences::load().applyTypography(theme_);
    for (QAction *action : themeActions_->actions())
        action->setChecked(action->data().toString() == theme_.name);
    ThemeStyle::apply(theme_);
    titleBar_->applyTheme(theme_);
    frame_->applyTheme(theme_);
    branchButton_->setIcon(UiIcons::icon(QStringLiteral("branch"), theme_.mutedText));
    fileTree_->setSearchHighlight(theme_.findHighlight);
    fileTree_->setDarkIcons(theme_.isDark);
    filesPanel_->setIconColor(theme_.foreground);
    claudeConfigView_->setDarkIcons(theme_.isDark);
    claudeConfigView_->setIconColor(theme_.foreground);
    problemsView_->setDarkIcons(theme_.isDark);
    sidebar_->applyTheme(theme_);
    editorTabs_->applyTheme(theme_);
    for (int i = 0; i < terminalTabs_->count(); ++i) {
        if (auto *terminal = qobject_cast<TerminalWidget *>(terminalTabs_->widget(i)))
            terminal->applyTheme(theme_);
    }
    changesView_->applyTheme(theme_);
    historyView_->applyTheme(theme_);
    findBar_->applyTheme(theme_);
    findInFiles_->applyTheme(theme_);
    serverNotice_->applyTheme(theme_);
    unsavedEditNotice_->applyTheme(theme_);
    toolRequestBar_->applyTheme(theme_);
    toolsNotice_->applyTheme(theme_);
    projectSettings_->applyTheme(theme_);
    if (preferencesPopup_)
        preferencesPopup_->applyTheme(theme_);
    snapshotsPopup_->applyTheme(theme_);
    newBranchPopup_->applyTheme(theme_);
    branchesPopup_->applyTheme(theme_);
    stashesPopup_->applyTheme(theme_);
    if (modeTabs_) {
        updateTabIcons();
        updateClaudeIndicator();
        updateUsage();
    }
    QSettings().setValue(QStringLiteral("theme"), theme_.name);
}

void MainWindow::showPreferences()
{
    // Built on first use: it lists every font of the system.
    if (!preferencesPopup_) {
        preferencesPopup_ = new PreferencesPopup(theme_, this);
        preferencesPopup_->setInstalledServers(languageServers_->installed());
        connect(preferencesPopup_, &PreferencesPopup::applied, this, [this] {
            applyPreferences(preferencesPopup_->preferences());
            Keymap::saveOverrides(preferencesPopup_->shortcutOverrides());
            applyKeymap(preferencesPopup_->shortcutOverrides());
        });
    }
    preferencesPopup_->popup(Preferences::load(), Keymap::commands(titleBar_->menuBar()), Keymap::loadOverrides());
}

void MainWindow::applyFullLine(const Preferences &preferences)
{
    fullLineModel_->configure(preferences.fullLineServer == QLatin1String("llama.cpp")
                                  ? LocalFullLineModel::Server::LlamaCpp
                                  : LocalFullLineModel::Server::Ollama,
                              QUrl(preferences.fullLineEndpoint), preferences.fullLineModel);
    completion_->setFullLineModel(preferences.fullLineCompletion ? fullLineModel_ : nullptr);
}

void MainWindow::applyPreferences(const Preferences &preferences)
{
    const Preferences previous = Preferences::load();
    preferences.save();
    if (preferences.interfaceFont() != previous.interfaceFont())
        QApplication::setFont(preferences.interfaceFont());
    if (preferences.theme != previous.theme || preferences.codeFont() != previous.codeFont() ||
        preferences.codeLineHeight != previous.codeLineHeight || preferences.ligatures != previous.ligatures)
        setTheme(preferences.theme);
    if (preferences.treeFont() != previous.treeFont())
        applyTreeFont(preferences.treeFont());
    // The menus' commands apply their own settings.
    systemTitleBar_->setChecked(preferences.systemTitleBar);
    formatOnSave_->setChecked(preferences.formatOnSave);
    snapshotBeforePrompt_->setChecked(preferences.snapshotBeforePrompt);
    shareEditorContext_->setChecked(preferences.shareEditorContext);
    exposeTools_->setChecked(preferences.exposeTools);
    claudeNotifications_->setChecked(preferences.claudeNotifications);
    editorTabs_->setEditorOptions(preferences.editor);
    languageServers_->setEnabled(preferences.languageServers);
    warmUpLanguageServers();
    applyFullLine(preferences);
    filesPanel_->setAlwaysSelectOpenedFile(preferences.alwaysSelectOpenedFile);
}

// Files, outline and changes: same proportions everywhere.
void MainWindow::applyTreeFont(const QFont &font)
{
    for (QTreeView *tree : findChildren<QTreeView *>())
        applyTreeMetrics(tree, font);
}

void MainWindow::applyKeymap(const Keymap::Overrides &overrides)
{
    const QList<Keymap::Command> commands = Keymap::commands(titleBar_->menuBar());
    Keymap::apply(commands, overrides);
    EditorWidget::setReservedKeys(Keymap::shortcutsInUse(commands));
}

void MainWindow::setMode(Mode mode)
{
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

void MainWindow::onHookReceived(const HookEvent &event)
{
    claudeActivity_.handle(event);
    switch (event.kind) {
    case HookEvent::Kind::PromptSubmitted:
        shareEditorContext();
        snapshotBeforePrompt(event.prompt);
        claudeTouched_.clear(); // the dots show the last prompt's files
        updateClaudeMarks();
        break;
    case HookEvent::Kind::EditRequested:
        guardUnsavedEdits(event.filePaths);
        break;
    case HookEvent::Kind::FilesEdited:
    case HookEvent::Kind::CommandFinished: {
        if (event.kind == HookEvent::Kind::FilesEdited) {
            followDiagramEdits(event.filePaths);
            recentFiles_.markEditedByClaude(event.filePaths);
            for (const QString &path : event.filePaths)
                claudeTouched_.insert(QDir::cleanPath(path));
            updateClaudeMarks();
        }
        fileStatuses_->refresh();
        claudeConfigView_->scheduleRefresh();
        // A command may have changed any file.
        const QStringList paths =
            event.kind == HookEvent::Kind::FilesEdited ? event.filePaths : editorTabs_->openFilePaths();
        for (const QString &path : paths)
            editorTabs_->syncWithDisk(path);
        symbolIndex_->refresh();
        if (mode_ == Mode::Diff)
            changesView_->refresh();
        break;
    }
    case HookEvent::Kind::ResponseFinished:
        onRepositoryChanged();
        statusBar()->showMessage(event.failed ? tr("Claude stopped on an error") : tr("Claude response finished"),
                                 statusMessageMs);
        break;
    case HookEvent::Kind::StatusUpdated:
        claudeStatus_ = event.status;
        updateUsage();
        break;
    case HookEvent::Kind::PermissionRequested:
    case HookEvent::Kind::NeedsAttention:
        break;
    }
}

void MainWindow::setSystemTitleBar(bool system)
{
#ifdef Q_OS_MACOS
    Q_UNUSED(system);
    // Duet's bar under the transparent system title bar, the traffic
    // lights at its left (Qt 6.9 and later).
    titleBar_->setWindowControlsVisible(false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    setWindowFlag(Qt::ExpandedClientAreaHint, true);
    setWindowFlag(Qt::NoTitleBarBackgroundHint, true);
    titleBar_->setLeadingSpace(72);
#endif
#else
    frame_->setFrameless(!system);
    titleBar_->setWindowControlsVisible(!system);
    statusBar()->setSizeGripEnabled(system); // Duet's frame resizes from its edges
#endif
}

void MainWindow::updateProjectMenu()
{
    projectMenu_->clear();
    projectMenu_->addAction(tr("Project Settings…"), this, &MainWindow::showProjectSettings);
    projectMenu_->addSeparator();
    int listed = 0;
    for (const ProjectConfig &project : ProjectStore::all()) {
        if (project.path == folder_ || !QFileInfo(project.path).isDir() || listed == 10)
            continue;
        const QString folder = project.path;
        QAction *action =
            projectMenu_->addAction(QFileInfo(folder).fileName(), this, [this, folder] { openFolder(folder); });
        action->setToolTip(QDir::toNativeSeparators(folder));
        ++listed;
    }
    projectMenu_->setToolTipsVisible(true);
    if (listed > 0)
        projectMenu_->addSeparator();
    projectMenu_->addAction(tr("Open Folder…"), QKeySequence(tr("Ctrl+K, Ctrl+O")), this, &MainWindow::chooseFolder);
    projectMenu_->addAction(tr("Close Project"), this, &MainWindow::closeProject);
    titleBar_->setProject(QFileInfo(folder_).fileName(), QDir::toNativeSeparators(folder_), projectMenu_);
}

// Line icons of the modes, side panel and terminals, in the theme's colors.
void MainWindow::updateTabIcons()
{
    const QColor color = theme_.mutedText;
    constexpr std::array<const char *, 4> modes{"terminal", "code", "diff", "history"};
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
                                                                         problemsView_->warningCount(), color)));
    newTerminalButton_->setIcon(UiIcons::icon(QStringLiteral("plus"), color));
    syncButton_->setIcon(UiIcons::icon(QStringLiteral("refresh"), color));
    cancelGitButton_->setIcon(UiIcons::icon(QStringLiteral("stop"), color));
    emptyNewTerminalButton_->setIcon(UiIcons::icon(QStringLiteral("plus"), color));
    for (int i = 0; i < terminalTabs_->count(); ++i) {
        const QWidget *terminal = terminalTabs_->widget(i);
        if (terminal == claudeTerminal_)
            continue; // its icon shows Claude's state
        // A run's: its kind, colored by its outcome.
        const QString runIcon = terminal->property("runIcon").toString();
        const QColor runColor = terminal->property("runColor").value<QColor>();
        terminalTabs_->setTabIcon(i, UiIcons::icon(runIcon.isEmpty() ? QStringLiteral("terminal") : runIcon,
                                                   runColor.isValid() ? runColor : color));
    }
}

void MainWindow::updateClaudeIndicator()
{
    const ClaudeActivity::State state = claudeActivity_.state();
    const QIcon icon = activityIcon(state, theme_);
    const QString description = claudeActivity_.description();
    // The activity dot replaces the tab's icon while Claude works, waits or
    // has finished.
    const bool idle = state == ClaudeActivity::State::Idle;
    if (claudeTerminal_) {
        const int index = terminalTabs_->indexOf(claudeTerminal_);
        terminalTabs_->setTabIcon(index, idle ? UiIcons::icon(QStringLiteral("claude"), QColor("#d97757")) : icon);
        terminalTabs_->setTabToolTip(index, description);
    }
    modeTabs_->setTabIcon(0, idle ? UiIcons::icon(QStringLiteral("terminal"), theme_.mutedText) : icon);
    modeTabs_->updateGeometry(); // see updateTabIcons()
    QString label;
    switch (state) {
    case ClaudeActivity::State::Working:
        label = tr("Claude is working");
        break;
    case ClaudeActivity::State::NeedsInput:
        label = tr("Claude needs you");
        break;
    case ClaudeActivity::State::Done:
        label = tr("Claude has finished");
        break;
    case ClaudeActivity::State::Idle:
        break;
    }
    titleBar_->setClaudeState(icon, label,
                              description + QLatin1Char('\n') + tr("Show the Claude session (Ctrl+Shift+L)"));
    const QString modeHint = tr("Full-screen terminals (Ctrl+1)");
    modeTabs_->setTabToolTip(0, state == ClaudeActivity::State::Idle ? modeHint
                                                                     : description + QLatin1Char('\n') + modeHint);
}

void MainWindow::updateClaudeWatched()
{
    claudeActivity_.setWatched(claudeTerminal_ && isActiveWindow() && mode_ == Mode::Terminal &&
                               terminalTabs_->currentWidget() == claudeTerminal_);
}

void MainWindow::notify(const QString &title, const QString &message)
{
    if (!claudeNotifications_->isChecked())
        return;
    QApplication::alert(this);
    // In the window, the dot on the tabs is enough.
    if (isActiveWindow() || !QSystemTrayIcon::isSystemTrayAvailable() || !QSystemTrayIcon::supportsMessages())
        return;
    if (!trayIcon_) {
        QIcon icon = QApplication::windowIcon();
        if (icon.isNull())
            icon = style()->standardIcon(QStyle::SP_MessageBoxInformation);
        trayIcon_ = new QSystemTrayIcon(icon, this);
        trayIcon_->setToolTip(QStringLiteral("Duet"));
        const auto showSession = [this] {
            showNormal();
            raise();
            activateWindow();
            showClaudeSession();
        };
        connect(trayIcon_, &QSystemTrayIcon::messageClicked, this, showSession);
        connect(trayIcon_, &QSystemTrayIcon::activated, this, showSession);
    }
    trayIcon_->show();
    trayIcon_->showMessage(title, message, QSystemTrayIcon::Information, notificationMs);
}

void MainWindow::registerMcpTools()
{
    DuetToolContext context;
    context.folder = [this] { return folder_; };
    context.tabs = editorTabs_;
    context.navigator = navigator_;
    context.symbols = symbolIndex_;
    context.languageServers = languageServers_;
    context.problems = problemStore_;
    context.intelligence = intelligence_;
    context.git = gitCommands_;
    context.menus = titleBar_->menuBar();
    context.showMessage = [this](const QString &message) { statusBar()->showMessage(message, 5000); };
    context.terminals = [this] {
        QList<std::pair<QString, TerminalWidget *>> terminals;
        for (int i = 0; i < terminalTabs_->count(); ++i) {
            if (auto *terminal = qobject_cast<TerminalWidget *>(terminalTabs_->widget(i)))
                terminals.append({terminalTabs_->tabText(i), terminal});
        }
        return terminals;
    };
    context.addTerminal = [this](const QString &title, const ProcessSpec &spec) { return addTerminal(title, spec); };
    context.showChanges = [this](const QString &relativePath) {
        setMode(Mode::Diff);
        changesView_->refresh();
        if (!relativePath.isEmpty())
            changesView_->selectFile(relativePath);
    };
    context.repositoryChanged = [this] { onRepositoryChanged(); };
    DuetTools::registerEditorTools(mcpRegistry_, context);
    DuetTools::registerWorkspaceTools(mcpRegistry_, context);
    DuetTools::registerCodeTools(mcpRegistry_, context);
    DuetTools::registerTerminalTools(mcpRegistry_, context);
    DuetTools::registerGitTools(mcpRegistry_, context);
#ifdef DUET_WITH_DIAGRAMS
    DuetTools::registerDiagramTools(mcpRegistry_, context);
#endif
    mcpRegistry_.setConfirmer([this](const QString &tool, const QString &what, std::function<void(bool)> answer) {
        toolRequests_.append({tr("Claude asks to %1 (%2).").arg(what, tool), std::move(answer)});
        if (toolRequests_.size() == 1)
            showToolRequest();
    });
    hookServer_.setMcpRegistry(&mcpRegistry_);

    // Subscribed clients hear when the user moves in the editor (once
    // things settle) and when the repository changes.
    auto *editorMoved = new QTimer(this);
    editorMoved->setSingleShot(true);
    editorMoved->setInterval(400);
    connect(editorMoved, &QTimer::timeout, this,
            [this] { mcpRegistry_.notifyResourceUpdated(QStringLiteral("duet://editor/state")); });
    connect(editorTabs_, &QTabWidget::currentChanged, editorMoved, qOverload<>(&QTimer::start));
    connect(editorTabs_, &EditorTabs::editorOpened, this, [editorMoved](EditorWidget *editor) {
        connect(editor, &QsciScintilla::cursorPositionChanged, editorMoved, qOverload<>(&QTimer::start));
        connect(editor, &QsciScintilla::selectionChanged, editorMoved, qOverload<>(&QTimer::start));
        connect(editor, &QsciScintilla::modificationChanged, editorMoved, qOverload<>(&QTimer::start));
    });
}

void MainWindow::showToolRequest()
{
    if (toolRequests_.isEmpty())
        return;
    toolRequestBar_->showNotice(toolRequests_.first().text, tr("Allow"));
    // Close refuses; the request waits for an answer.
    QApplication::alert(this);
}

void MainWindow::shareEditorContext()
{
    if (!shareEditorContext_->isChecked())
        return;
    const EditorContext context = EditorContext::of(*editorTabs_, folder_);
    const QString description = context.describe();
    if (!description.isEmpty())
        hookServer_.respond(EditorContext::promptHookOutput(description));
}

void MainWindow::guardUnsavedEdits(const QStringList &paths)
{
    QStringList unsaved;
    for (const QString &path : paths) {
        if (editorTabs_->hasUnsavedChangesUnder(path)) {
            const QString relative = QDir(folder_).relativeFilePath(path);
            unsaved.append(relative.startsWith(QLatin1String("..")) ? path : relative);
        }
    }
    if (unsaved.isEmpty())
        return;
    hookServer_.respond(UnsavedEditGuard::denyOutput(UnsavedEditGuard::reason(unsaved)));
    const QString files = unsaved.join(QStringLiteral(", "));
    unsavedEditNotice_->showNotice(
        tr("Claude did not edit %1: it has unsaved changes here. Save or discard them, then ask Claude again.")
            .arg(files),
        tr("Save All"));
    statusBar()->showMessage(tr("Edit by Claude refused, unsaved changes in %1").arg(files), statusMessageMs);
}

void MainWindow::followDiagramEdits(const QStringList &paths)
{
#ifdef DUET_WITH_DIAGRAMS
    for (const QString &path : paths) {
        if (!DiagramView::isDiagramFile(path))
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QStringList problems = DiagramCheck::problems(path, file.readAll());
        if (!problems.isEmpty()) {
            const QString relative = QDir(folder_).relativeFilePath(path);
            hookServer_.respond(DiagramCheck::feedbackOutput(relative, problems));
            return;
        }
        // A diagram Claude has just drawn shows up, laid out.
        if (!editorTabs_->diagramViewFor(path))
            editorTabs_->openFile(path);
    }
#else
    Q_UNUSED(paths);
#endif
}

void MainWindow::generateDiagram(const QString &folder)
{
    const QList<DiagramKind> kinds = DiagramKinds::available();
    if (kinds.isEmpty() || folder.isEmpty())
        return;
    const QString relative = QDir(folder_).relativeFilePath(folder);
    // At the root, diagrams go with the documentation.
    const QString initialName = relative == QLatin1String(".") ? QStringLiteral("docs/diagrams/") : QString();
    const QString location = relative == QLatin1String(".") ? QFileInfo(folder_).fileName() + QLatin1Char('/')
                                                            : QDir::toNativeSeparators(relative + QLatin1Char('/'));
    auto *popup = new DiagramRequestPopup(
        location, kinds, initialName,
        [this, folder](const QString &kindId, const QString &name, const QString &description) {
            const DiagramKind kind = DiagramKinds::find(kindId);
            if (description.isEmpty())
                return tr("Describe what the diagram should show");
            if (name.isEmpty() || name.endsWith(QLatin1Char('/')) || name.endsWith(QLatin1Char('\\')))
                return tr("Enter a file name");
            const EntryTarget target = resolveNewEntry(folder_, folder, DiagramKinds::withExtension(name, kind), false);
            if (!target.error.isEmpty())
                return target.error;
            drawDiagram(target.path, kind.title, description);
            return QString();
        },
        this);
    popup->popup();
}

void MainWindow::drawDiagram(const QString &path, const QString &kindTitle, const QString &description)
{
#ifdef DUET_WITH_DIAGRAMS
    setMode(Mode::Edit);
    auto *view = new DiagramGenerationView(folder_, path, kindTitle, description);
    editorTabs_->showPage(view, QFileInfo(path).fileName(), QStringLiteral("claude"));
    connect(view, &DiagramGenerationView::closeRequested, this, [this, view] { editorTabs_->closePage(view); });
    connect(view, &DiagramGenerationView::finished, this, [this, view](const QString &drawn, const QString &summary) {
        editorTabs_->replacePage(view, drawn);
        statusBar()->showMessage(summary.isEmpty() ? tr("Diagram drawn by Claude")
                                                   : summary.section(QLatin1Char('\n'), 0, 0),
                                 statusMessageMs);
    });
    view->start();
#else
    Q_UNUSED(path);
    Q_UNUSED(kindTitle);
    Q_UNUSED(description);
#endif
}

void MainWindow::snapshotBeforePrompt(const QString &prompt)
{
    if (!snapshotBeforePrompt_->isChecked())
        return;
    QString summary = prompt.section(QLatin1Char('\n'), 0, 0).simplified();
    if (summary.size() > snapshotPromptLength)
        summary = summary.left(snapshotPromptLength - 1) + QStringLiteral("…");
    const QString reason =
        summary.isEmpty() ? tr("Before a Claude prompt") : tr("Before the Claude prompt \u201c%1\u201d").arg(summary);
    // A dozen git commands in a large repository: they run off the UI
    // thread, and Claude waits for the snapshot before touching any file.
    gitCommands_->runQuietly(
        [folder = folder_, reason, taken = tr("Snapshot taken before the prompt")] {
            const std::optional<RepositoryInfo> info = RepositoryInfo::read(folder);
            if (!info || Snapshots::createOfWorkingTree(*info, reason).isEmpty())
                return QString();
            return taken;
        },
        [this, reply = hookServer_.deferReply()](const QString &message, const QString &error) {
            reply();
            if (!error.isEmpty())
                statusBar()->showMessage(tr("Snapshot before the prompt failed: %1").arg(error), statusMessageMs);
            else if (!message.isEmpty())
                statusBar()->showMessage(message, statusMessageMs);
        });
}

void MainWindow::updateUsage()
{
    const ClaudeUsage usage = ClaudeUsage::fromStatusJson(claudeStatus_);
    const QString summary = usage.summary();
    usageLabel_->setVisible(!summary.isEmpty());
    usageLabel_->setToolTip(usage.details());
    const char *warning = usageWarningColor(usage.highestPercent(), theme_.isDark);
    usageLabel_->setText(
        warning
            ? QStringLiteral("<span style=\"color:%1\">%2</span>").arg(QLatin1String(warning), summary.toHtmlEscaped())
            : summary);
}

void MainWindow::showCommandPalette()
{
    commandPalette_->popup(titleBar_->menuBar());
}

void MainWindow::onCurrentTabChanged()
{
    EditorWidget *editor = editorTabs_->currentEditor();
    findBar_->setEditor(editor);
    if (!editor && findBar_->isVisible())
        findBar_->close();
    editorStatus_->setEditor(mode_ == Mode::Edit ? editor : nullptr);
    const QString path = editorTabs_->currentFilePath();
    recentFiles_.touch(path);
    // Looked at: the file loses its dot.
    if (claudeTouched_.remove(path))
        updateClaudeMarks();
    if (filesPanel_->alwaysSelectOpenedFile() && !path.isEmpty())
        fileTree_->select(path);
    updateServerNotice();
}

void MainWindow::warmUpLanguageServer(EditorWidget *editor)
{
    languageServers_->warmUp(editor->filePath(), editor->languageId(), editor->utf8Bytes().toByteArray());
}

void MainWindow::warmUpLanguageServers()
{
    for (EditorWidget *editor : editorTabs_->editors())
        warmUpLanguageServer(editor);
}

void MainWindow::updateServerNotice()
{
    const EditorWidget *editor = editorTabs_->currentEditor();
    const QString failure = editor ? languageServers_->failure(editor->languageId()) : QString();
    if (failure.isEmpty() || dismissedServerNotices_.contains(failure)) {
        serverNotice_->hide();
        return;
    }
    serverNotice_->showNotice(tr("Completion is limited to the words of the project: %1").arg(failure), tr("Retry"));
}

void MainWindow::updateClaudeMarks()
{
    fileTree_->setClaudeTouched(claudeTouched_);
    claudeConfigView_->setClaudeTouched(claudeTouched_);
}

void MainWindow::selectOpenedFile()
{
    const QString path = editorTabs_->currentFilePath();
    if (path.isEmpty())
        return;
    setMode(Mode::Edit);
    sidebar_->showView(filesPanel_);
    if (!fileTree_->select(path)) {
        // Outside the scope: back to the whole project, as IntelliJ does.
        filesPanel_->setScopeIndex(0);
        if (!fileTree_->select(path)) {
            statusBar()->showMessage(tr("The current file is not in the project folder"), statusMessageMs);
            return;
        }
    }
    fileTree_->setFocus();
}

void MainWindow::sendToClaude(const QString &path)
{
    QString relative = QDir(folder_).relativeFilePath(path);
    if (relative.startsWith(QLatin1String(".."))) // outside the project (~/.claude...)
        relative = path;
    if (relative == QLatin1String("."))
        relative = QStringLiteral("./");
    else if (QFileInfo(path).isDir())
        relative += QLatin1Char('/');
    // A file mention in Claude Code's prompt, typed where the cursor is.
    typeInClaudePrompt(QStringLiteral("@%1 ").arg(QDir::toNativeSeparators(relative)), PromptEntry::Typed);
}

void MainWindow::createCodeRequestActions(QMenu *claude)
{
    const auto add = [this, claude](const QString &text, const QKeySequence &shortcut, auto slot) {
        QAction *action = claude->addAction(text, shortcut, this, slot);
        // Only while the editor has the focus: in the file tree, Ctrl+Alt+K
        // sends the selected file instead.
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        editorTabs_->addAction(action);
        codeRequestActions_.append(action);
    };
    add(tr("&Explain This Code"), {}, [this] { askClaude(CodeRequest::Kind::Explain); });
    add(tr("Re&view This Code"), {}, [this] { askClaude(CodeRequest::Kind::Review); });
    add(tr("Write T&ests for This Code"), {}, [this] { askClaude(CodeRequest::Kind::WriteTests); });
    add(tr("&Add to Claude's Prompt"), QKeySequence(tr("Ctrl+Alt+K")), [this] { addCodeToClaudePrompt(); });
}

void MainWindow::askClaude(CodeRequest::Kind kind)
{
    if (const std::optional<CodeRequest::Target> target = currentCodeTarget())
        typeInClaudePrompt(CodeRequest::prompt(kind, *target), PromptEntry::Submitted);
}

void MainWindow::addCodeToClaudePrompt()
{
    if (const std::optional<CodeRequest::Target> target = currentCodeTarget())
        typeInClaudePrompt(CodeRequest::mention(*target) + QLatin1Char(' '), PromptEntry::Typed);
}

std::optional<CodeRequest::Target> MainWindow::currentCodeTarget()
{
    const EditorWidget *editor = editorTabs_->currentEditor();
    std::optional<CodeRequest::Target> target = editor ? CodeRequest::targetIn(*editor, folder_) : std::nullopt;
    if (!target)
        statusBar()->showMessage(tr("Open a file to ask Claude about its code"), statusMessageMs);
    return target;
}

void MainWindow::typeInClaudePrompt(const QString &text, PromptEntry entry)
{
    if (!claudeTerminal_ || !claudeTerminal_->isRunning()) {
        QString error;
        if (!startClaudeSession(&error)) {
            QMessageBox::warning(this, tr("Claude Session"), error);
            return;
        }
    }
    claudeTerminal_->paste(text);
    if (entry == PromptEntry::Submitted) {
        // Apart from the paste: an Enter read with the pasted text may be
        // taken for a new line in the prompt.
        QTimer::singleShot(submitDelayMs, claudeTerminal_, &TerminalWidget::pressEnter);
    }
    terminalTabs_->setCurrentWidget(claudeTerminal_);
    setMode(Mode::Terminal);
}

void MainWindow::runGitOnTree(const QString &title, const QString &path, bool rollback)
{
    std::optional<RepositoryInfo> info;
    try {
        info = RepositoryInfo::read(folder_);
    } catch (const GitError &error) {
        QMessageBox::warning(this, tr("%1: failed").arg(title), QString::fromStdString(error.what()));
        return;
    }
    if (!info)
        return;
    const QDir folder(folder_);
    const QStringList paths = rollback ? trackedChangesUnder(path, fileStatuses_->statuses(), *info, folder)
                                       : QStringList{repositoryPathOf(path, *info, folder)};
    if (paths.isEmpty())
        return;
    const int count = static_cast<int>(paths.size());
    gitCommands_->run(title, [info = *info, paths, rollback, count] {
        if (rollback) {
            GitOperations::discard(info, paths);
            return count == 1 ? QObject::tr("1 file rolled back") : QObject::tr("%1 files rolled back").arg(count);
        }
        GitOperations::stage(info, paths);
        return QObject::tr("Added to the index");
    });
}

void MainWindow::showFind(bool withReplace)
{
    if (mode_ != Mode::Edit || !editorTabs_->currentEditor())
        return;
    editorTabs_->revealEditor(editorTabs_->currentEditor());
    if (withReplace)
        findBar_->showReplace();
    else
        findBar_->showFind();
}

void MainWindow::showGoTo(int tab)
{
    goToPopup_->popup(static_cast<GoToPopup::Tab>(tab));
}

void MainWindow::showFindInFiles()
{
    const EditorWidget *editor = mode_ == Mode::Edit ? editorTabs_->currentEditor() : nullptr;
    findInFiles_->popup(editor && editor->hasSelectedText() ? editor->selectedText() : QString());
}

void MainWindow::goToChange(bool forward)
{
    EditorWidget *editor = mode_ == Mode::Edit ? editorTabs_->currentEditor() : nullptr;
    if (!editor)
        return;
    editorTabs_->revealEditor(editor);
    ChangeGutter *gutter = editor->changeGutter();
    if (!(forward ? gutter->goToNext() : gutter->goToPrevious()))
        statusBar()->showMessage(
            gutter->hasBase() ? tr("No changes in this file") : tr("This file is not tracked by git"), statusMessageMs);
}

void MainWindow::rollbackChange()
{
    const EditorWidget *editor = mode_ == Mode::Edit ? editorTabs_->currentEditor() : nullptr;
    if (editor && !editor->changeGutter()->rollbackAtCursor())
        statusBar()->showMessage(tr("No change on this line"), statusMessageMs);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        if (isActiveWindow() && trayIcon_)
            trayIcon_->hide();
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
            claudeActivity_.interrupt();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateRepositoryInfo()
{
    std::optional<RepositoryInfo> info;
    try {
        info = RepositoryInfo::read(folder_);
    } catch (const GitError &error) {
        // An unreadable repository: no branch shown.
        qDebug() << "Repository of" << folder_ << "unreadable:" << error.what();
    }
    repositoryWatcher_->setGitDir(info ? info->gitDir : QString());
    if (inRepository_ != info.has_value()) {
        inRepository_ = info.has_value();
        fileTree_->setStatuses(fileStatuses_->statuses(), inRepository_);
    }
    fileStatuses_->refresh();
    const QString operation = info ? info->operation : QString();
    continueAction_->setEnabled(Integration::canContinue(operation));
    abortAction_->setEnabled(Integration::canContinue(operation));
    skipAction_->setEnabled(Integration::canSkip(operation));
    if (!info) {
        branchButton_->hide();
        syncButton_->hide();
        return;
    }

    QString text = info->detached ? tr("Detached HEAD (%1)").arg(info->headId) : info->branch;
    if (info->ahead > 0)
        text += QStringLiteral(" ↑%1").arg(info->ahead);
    if (info->behind > 0)
        text += QStringLiteral(" ↓%1").arg(info->behind);
    if (info->unborn)
        text += tr(" — no commits");
    if (!operation.isEmpty())
        text += tr(" — %1 in progress").arg(operation);
    branchButton_->setText(text);
    QString tooltip = info->unborn
                          ? tr("Repository %1").arg(info->workDir)
                          : tr("Repository %1\nLast commit %2: %3")
                                .arg(info->workDir, info->headId, info->headMessage.section(QLatin1Char('\n'), 0, 0));
    if (!info->upstream.isEmpty())
        tooltip += tr("\nTracking %1: %2 ahead, %3 behind as of the last fetch")
                       .arg(info->upstream)
                       .arg(info->ahead)
                       .arg(info->behind);
    branchButton_->setToolTip(tooltip + tr("\nClick for the branches (Ctrl+Alt+B)"));
    branchButton_->show();

    syncButton_->setVisible(!info->detached && !info->unborn);
    syncButton_->setToolTip(info->upstream.isEmpty() ? tr("Publish %1 to the remote").arg(info->branch)
                                                     : tr("Sync with %1: fetch, pull, then push").arg(info->upstream));
}

void MainWindow::onRepositoryChanged()
{
    mcpRegistry_.notifyResourceUpdated(QStringLiteral("duet://git/status"));
    updateRepositoryInfo();
    symbolIndex_->refresh();          // branch switch, pull…
    editorTabs_->reloadChangeBases(); // staging, commit…
    if (mode_ == Mode::Diff && !gitCommands_->isBusy())
        changesView_->refresh();
    if (mode_ == Mode::History)
        historyView_->refresh();
}

void MainWindow::showNewBranch()
{
    std::optional<RepositoryInfo> repository;
    QStringList branches;
    try {
        repository = RepositoryInfo::read(folder_);
        if (repository && !repository->unborn)
            branches = Branches::local(repository->workDir);
    } catch (const GitError &error) {
        statusBar()->showMessage(QString::fromStdString(error.what()), statusMessageMs);
        return;
    }
    if (!repository) {
        statusBar()->showMessage(tr("The folder is not in a git repository"), statusMessageMs);
        return;
    }
    newBranchPopup_->popup(*repository, branches);
}

void MainWindow::createBranch(const QString &name, bool switchTo)
{
    gitCommands_->run(
        tr("New branch"),
        [folder = folder_, name, switchTo] {
            const std::optional<RepositoryInfo> repository = RepositoryInfo::read(folder);
            if (!repository)
                throw GitError(QObject::tr("The folder is not in a git repository"));
            Branches::create(*repository, name, switchTo);
            return switchTo ? QObject::tr("Switched to the new branch %1").arg(name)
                            : QObject::tr("Branch %1 created").arg(name);
        },
        [this] { onRepositoryChanged(); });
}

void MainWindow::showBranches()
{
    branchesPopup_->popup(folder_);
}

void MainWindow::showStashes()
{
    stashesPopup_->popup(folder_);
}

std::optional<QString> MainWindow::askCredentials(const QString &prompt)
{
    // ssh's unknown host: "Are you sure you want to continue connecting
    // (yes/no/[fingerprint])?", after the key's fingerprint.
    if (AskpassServer::isConfirmation(prompt)) {
        QMessageBox box(QMessageBox::Question, tr("Unknown Host"), prompt.trimmed(), QMessageBox::Cancel, this);
        const QPushButton *trust = box.addButton(tr("Trust and Connect"), QMessageBox::AcceptRole);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        return box.clickedButton() == trust ? QStringLiteral("yes") : QStringLiteral("no");
    }
    // A user name is shown as typed; passwords and passphrases are not.
    const bool secret = !prompt.startsWith(QLatin1String("Username"), Qt::CaseInsensitive);
    bool accepted = false;
    QString answer = QInputDialog::getText(this, tr("Git Credentials"), prompt.trimmed(),
                                           secret ? QLineEdit::Password : QLineEdit::Normal, {}, &accepted);
    if (!accepted)
        return std::nullopt;
    return answer;
}

void MainWindow::showSnapshots()
{
    snapshotsPopup_->popup(folder_);
}

void MainWindow::showPrerequisites()
{
    if (!prerequisitesDialog_) {
        // Not modal: installations run in a terminal tab of this window.
        prerequisitesDialog_ = new PrerequisitesDialog(this);
        connect(prerequisitesDialog_, &PrerequisitesDialog::installRequested, this,
                [this](const QString &title, const ProcessSpec &spec) {
                    const TerminalWidget *terminal = addTerminal(title, spec);
                    connect(terminal, &TerminalWidget::finished, prerequisitesDialog_, [this] {
                        prerequisitesDialog_->recheck();
                        prerequisitesDialog_->show();
                        prerequisitesDialog_->raise();
                    });
                });
        connect(prerequisitesDialog_, &PrerequisitesDialog::checked, this, [this] {
            // Freshly installed programs: use them right away.
            updateRepositoryInfo();
            if (mode_ == Mode::Diff)
                changesView_->refresh();
            const bool claudeFound = std::any_of(
                prerequisitesDialog_->prerequisites().begin(), prerequisitesDialog_->prerequisites().end(),
                [](const Prerequisite &p) {
                    return p.id == Prerequisite::Id::ClaudeCode && p.status != Prerequisite::Status::Missing;
                });
            if (claudeFound && (!claudeTerminal_ || !claudeTerminal_->isRunning()))
                startClaudeSession();
        });
    } else {
        prerequisitesDialog_->recheck();
    }
    prerequisitesDialog_->show();
    prerequisitesDialog_->raise();
    prerequisitesDialog_->activateWindow();
}

QString MainWindow::absolutePath(const QString &relativePath) const
{
    return QDir(folder_).filePath(relativePath);
}
