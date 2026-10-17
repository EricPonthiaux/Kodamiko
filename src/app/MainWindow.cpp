#include "MainWindow.h"

#include "changes/ChangesView.h"
#include "changes/NewBranchPopup.h"
#include "changes/SnapshotsPopup.h"
#include "claude/ClaudeLauncher.h"
#include "claude/ClaudeUsage.h"
#include "completion/CompletionController.h"
#include "completion/CodeGeneration.h"
#include "completion/CompletionService.h"
#include "completion/DocComments.h"
#include "completion/FullLine.h"
#include "completion/LocalCompletion.h"
#include "completion/NameSuggestions.h"
#include "completion/Templates.h"
#include "editor/ChangeGutter.h"
#include "editor/EditorStatus.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/FindBar.h"
#include "git/Branches.h"
#include "git/FileStatuses.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"
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
#include "ui/TrailingTabButton.h"
#include "ui/TreeMetrics.h"
#include "ui/UiIcons.h"
#include "ui/WindowFrame.h"
#include "welcome/ProjectSettingsPopup.h"
#include "welcome/ProjectSetupForm.h"
#include "workspace/CommandPalette.h"
#include "workspace/FilesPanel.h"
#include "workspace/FileTreeView.h"
#include "workspace/SidePanel.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
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
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int statusMessageMs = 5000;
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

} // namespace

MainWindow::MainWindow(const QString &folder, QWidget *parent)
    : QMainWindow(parent)
    , theme_(Theme::named(savedThemeName()))
{
    // The saved theme from the start: the widgets are styled as they are
    // created, rather than all restyled when restoreSettings() applies it.
    theme_.font = Preferences::load().codeFont();
    ThemeStyle::apply(theme_);
    createWidgets();
    createMenus();
    applyKeymap(Keymap::loadOverrides());
    restoreSettings();

    connect(&hookServer_, &HookServer::hookReceived, this, &MainWindow::onHookReceived);
    connect(&claudeActivity_, &ClaudeActivity::stateChanged, this, &MainWindow::updateClaudeIndicator);
    connect(&claudeActivity_, &ClaudeActivity::attentionRequested, this, &MainWindow::notify);
    if (!hookServer_.listen())
        statusBar()->showMessage(tr("Claude tracking unavailable: %1").arg(hookServer_.errorString()));

    const QString lastFolder = QSettings().value(QStringLiteral("lastFolder")).toString();
    openFolder(!folder.isEmpty() ? folder : !lastFolder.isEmpty() ? lastFolder : QDir::homePath());
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
        auto *controller = new CompletionController(editor, completion_);
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
    intelligence_ = new CodeIntelligence(editorTabs_, languageServers_, navigator_, this);
    connect(editorTabs_, &EditorTabs::editorOpened, intelligence_, &CodeIntelligence::attach);
    connect(editorTabs_, &EditorTabs::definitionRequested, intelligence_, &CodeIntelligence::goToDefinition);
    connect(intelligence_, &CodeIntelligence::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });
    connect(navigator_, &CodeNavigator::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });

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
    repositoryWatcher_ = new RepositoryWatcher(this);
    connect(repositoryWatcher_, &RepositoryWatcher::changed, this, &MainWindow::onRepositoryChanged);

    changesView_ = new ChangesView(theme_, gitCommands_, this);
    connect(changesView_, &ChangesView::openFileRequested, this, [this](const QString &relativePath) {
        setMode(Mode::Edit);
        editorTabs_->openFile(absolutePath(relativePath));
    });
    connect(changesView_, &ChangesView::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });

    historyView_ = new HistoryView(theme_, this);
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
    setCentralWidget(modes_);
    createModeTabs();
    commandPalette_ = new CommandPalette(this);

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

    applyTreeFont(Preferences::load().treeFont());

    resize(1400, 900);
}

// Edit mode: side panel (Files / Outline tabs) | editor tabs.
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
    fileTree_->setUnsavedChangesCheck([this](const QString &path) { return editorTabs_->hasUnsavedChangesUnder(path); });
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

    outlineView_ = new OutlineView(this);
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
        if (EditorWidget *editor = editorTabs_->currentEditor())
            languageServers_->retry(editor->languageId());
    });
    connect(serverNotice_, &NoticeBar::dismissed, this, [this] {
        if (EditorWidget *editor = editorTabs_->currentEditor())
            dismissedServerNotices_.insert(languageServers_->failure(editor->languageId()));
    });
    connect(languageServers_, &LanguageServers::serverFailed, this, &MainWindow::updateServerNotice);
    editorLayout->addWidget(serverNotice_);
    editorLayout->addWidget(editorTabs_, 1);

    // Files and Outline as KDevelop's tool views: vertical tabs at the
    // window's edge, the panel next to them.
    sidebar_ = new SidePanel(this);
    sidebar_->addView(filesPanel_, {}, tr("Files"));
    sidebar_->addView(outlineView_, {}, tr("Outline"));

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
    modeTabs_->addTab(tr("Terminal"));
    modeTabs_->addTab(tr("Edit"));
    modeTabs_->addTab(tr("Diff"));
    modeTabs_->addTab(tr("History"));
    modeTabs_->setTabToolTip(0, tr("Full-screen terminals (Ctrl+1)"));
    modeTabs_->setTabToolTip(1, tr("File tree and code editor (Ctrl+2)"));
    modeTabs_->setTabToolTip(2, tr("Pending changes, staging and commit (Ctrl+3)"));
    modeTabs_->setTabToolTip(3, tr("Past commits, their files and their diffs (Ctrl+4)"));
    connect(modeTabs_, &QTabBar::currentChanged, this, [this](int index) { setMode(static_cast<Mode>(index)); });

    // Status bar messages stay displayed to the left of the
    // branch and the tabs.
    modeTabs_->setShape(QTabBar::RoundedSouth);
    editorStatus_ = new EditorStatus(this);
    statusBar()->addPermanentWidget(editorStatus_);
    usageLabel_ = new QLabel(this);
    usageLabel_->setContentsMargins(8, 0, 8, 0);
    usageLabel_->hide();
    statusBar()->addPermanentWidget(usageLabel_);
    // The branch, at the bottom; a click opens the Git menu.
    branchButton_ = new QToolButton(this);
    branchButton_->setObjectName(QStringLiteral("chip"));
    branchButton_->setAutoRaise(true);
    branchButton_->setFocusPolicy(Qt::NoFocus);
    branchButton_->setPopupMode(QToolButton::InstantPopup);
    branchButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    branchButton_->hide();
    statusBar()->addPermanentWidget(branchButton_);
    statusBar()->addPermanentWidget(modeTabs_);
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
    claude->addAction(tr("&Claude Session"), QKeySequence(tr("Ctrl+Shift+L")), this,
                      &MainWindow::showClaudeSession);
    claude->addAction(tr("New &Terminal"), QKeySequence(tr("Ctrl+Shift+T")), this, &MainWindow::newShell);
    claude->addSeparator();
    claude->addAction(tr("Restore the State Before a &Prompt…"), this, &MainWindow::showSnapshots);
    QSettings settings;
    snapshotBeforePrompt_ = claude->addAction(tr("&Snapshot Before Each Prompt"));
    snapshotBeforePrompt_->setCheckable(true);
    snapshotBeforePrompt_->setChecked(settings.value(QStringLiteral("snapshotBeforePrompt"), true).toBool());
    snapshotBeforePrompt_->setToolTip(tr("Keep a copy of the working directory before Claude works on a prompt, to undo its changes"));
    connect(snapshotBeforePrompt_, &QAction::toggled, this,
            [](bool checked) { QSettings().setValue(QStringLiteral("snapshotBeforePrompt"), checked); });
    claudeNotifications_ = claude->addAction(tr("&Notifications"));
    claudeNotifications_->setCheckable(true);
    claudeNotifications_->setChecked(settings.value(QStringLiteral("claudeNotifications"), true).toBool());
    claudeNotifications_->setToolTip(tr("Alert when Claude needs you or has finished while you are elsewhere"));
    connect(claudeNotifications_, &QAction::toggled, this,
            [](bool checked) { QSettings().setValue(QStringLiteral("claudeNotifications"), checked); });

    createNavigateMenu();
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
        if (EditorWidget *editor = editorTabs_->currentEditor(); editor && mode_ == Mode::Edit)
            if (CompletionController *completion = CompletionController::of(editor))
                completion->complete();
    });
    edit->addAction(tr("S&mart Complete Code"), QKeySequence(tr("Ctrl+Shift+Space")), this, [this] {
        if (EditorWidget *editor = editorTabs_->currentEditor(); editor && mode_ == Mode::Edit)
            if (CompletionController *completion = CompletionController::of(editor))
                completion->completeSmart();
    });
    const auto inCode = [this](void (CodeIntelligence::*command)()) {
        return [this, command] {
            if (mode_ == Mode::Edit)
                (intelligence_->*command)();
        };
    };
    edit->addAction(tr("&Parameter Info"), QKeySequence(tr("Ctrl+P")), this, inCode(&CodeIntelligence::showParameterInfo));
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
    formatOnSave_->setChecked(QSettings().value(QStringLiteral("formatOnSave"), false).toBool());
    formatOnSave_->setToolTip(tr("Format each file with its language's formatter before saving it"));
    editorTabs_->setFormatOnSave(formatOnSave_->isChecked());
    connect(formatOnSave_, &QAction::toggled, this, [this](bool checked) {
        editorTabs_->setFormatOnSave(checked);
        QSettings().setValue(QStringLiteral("formatOnSave"), checked);
    });
    edit->addSeparator();
    const auto inEditor = [this](void (EditorWidget::*fold)(bool), bool folds) {
        return [this, fold, folds] {
            if (EditorWidget *editor = editorTabs_->currentEditor(); editor && mode_ == Mode::Edit)
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
    edit->addSeparator();
    findInFilesAction_ =
        edit->addAction(tr("Find in F&iles…"), QKeySequence(tr("Ctrl+Shift+F")), this, &MainWindow::showFindInFiles);
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
    navigate->addAction(tr("&Next Change"), QKeySequence(tr("Ctrl+Alt+Shift+Down")), this, [this] { goToChange(true); });
    navigate->addAction(tr("&Previous Change"), QKeySequence(tr("Ctrl+Alt+Shift+Up")), this,
                        [this] { goToChange(false); });
    navigate->addAction(tr("R&ollback Lines"), QKeySequence(tr("Ctrl+Alt+Z")), this, &MainWindow::rollbackChange);
}

void MainWindow::createHelpMenu()
{
    QMenu *help = titleBar_->menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Check &Prerequisites…"), this, &MainWindow::showPrerequisites);
}

void MainWindow::createGitMenu()
{
    QMenu *git = titleBar_->menuBar()->addMenu(tr("&Git"));
    gitMenu_ = git;
    git->addAction(tr("&Prepare Commit"), QKeySequence(tr("Ctrl+Shift+G")), this, [this] {
        setMode(Mode::Diff);
        changesView_->focusCommitMessage();
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
    git->addAction(tr("&New Branch…"), this, &MainWindow::showNewBranch);
    git->addSeparator();
    git->addAction(tr("Restore a S&napshot…"), this, &MainWindow::showSnapshots);
}

void MainWindow::restoreSettings()
{
    QSettings settings;
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
    toolsNotice_->showNotice(tr("This project's setup needs tools Duet has not downloaded yet: %1")
                                 .arg(InstallPlan::summary(steps)),
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
bool MainWindow::startClaudeSession(QString *error)
{
    if (claudeTerminal_) {
        terminalTabs_->removeTab(terminalTabs_->indexOf(claudeTerminal_));
        claudeTerminal_->deleteLater();
        claudeTerminal_ = nullptr;
    }
    QString launchError;
    const std::optional<ProcessSpec> spec =
        ClaudeLauncher::claudeProcess(folder_, hookServer_.serverName(), &launchError);
    if (!spec) {
        statusBar()->showMessage(tr("Claude session unavailable: %1").arg(launchError));
        if (error)
            *error = launchError;
        updateTerminalPage();
        return false;
    }
    claudeTerminal_ = new TerminalWidget(terminalTabs_);
    claudeTerminal_->applyTheme(theme_);
    claudeTerminal_->installEventFilter(this); // Escape interrupts the response
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
    theme_.font = Preferences::load().codeFont();
    for (QAction *action : themeActions_->actions())
        action->setChecked(action->data().toString() == theme_.name);
    ThemeStyle::apply(theme_);
    titleBar_->applyTheme(theme_);
    frame_->applyTheme(theme_);
    branchButton_->setIcon(UiIcons::icon(QStringLiteral("branch"), theme_.mutedText));
    fileTree_->setSearchHighlight(theme_.findHighlight);
    fileTree_->setDarkIcons(theme_.isDark);
    filesPanel_->setIconColor(theme_.foreground);
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
    toolsNotice_->applyTheme(theme_);
    projectSettings_->applyTheme(theme_);
    if (preferencesPopup_)
        preferencesPopup_->applyTheme(theme_);
    snapshotsPopup_->applyTheme(theme_);
    newBranchPopup_->applyTheme(theme_);
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
    fullLineModel_->configure(preferences.fullLineServer == QLatin1String("llama.cpp") ? LocalFullLineModel::Server::LlamaCpp
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
    if (preferences.theme != previous.theme || preferences.codeFont() != previous.codeFont())
        setTheme(preferences.theme);
    if (preferences.treeFont() != previous.treeFont())
        applyTreeFont(preferences.treeFont());
    // The menus' commands apply their own settings.
    systemTitleBar_->setChecked(preferences.systemTitleBar);
    formatOnSave_->setChecked(preferences.formatOnSave);
    snapshotBeforePrompt_->setChecked(preferences.snapshotBeforePrompt);
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
        snapshotBeforePrompt(event.prompt);
        claudeTouched_.clear(); // the dots show the last prompt's files
        updateClaudeMarks();
        break;
    case HookEvent::Kind::FilesEdited:
    case HookEvent::Kind::CommandFinished: {
        if (event.kind == HookEvent::Kind::FilesEdited) {
            recentFiles_.markEditedByClaude(event.filePaths);
            for (const QString &path : event.filePaths)
                claudeTouched_.insert(QDir::cleanPath(path));
            updateClaudeMarks();
        }
        fileStatuses_->refresh();
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
        QAction *action = projectMenu_->addAction(QFileInfo(folder).fileName(), this, [this, folder] { openFolder(folder); });
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
    const char *modes[] = {"terminal", "code", "diff", "history"};
    for (int i = 1; i < 4; ++i)
        modeTabs_->setTabIcon(i, UiIcons::icon(QLatin1String(modes[i]), color));
    sidebar_->setViewIcon(filesPanel_, UiIcons::icon(QStringLiteral("files"), color));
    sidebar_->setViewIcon(outlineView_, UiIcons::icon(QStringLiteral("outline"), color));
    newTerminalButton_->setIcon(UiIcons::icon(QStringLiteral("plus"), color));
    emptyNewTerminalButton_->setIcon(UiIcons::icon(QStringLiteral("plus"), color));
    for (int i = 0; i < terminalTabs_->count(); ++i) {
        if (terminalTabs_->widget(i) != claudeTerminal_) // its icon shows Claude's state
            terminalTabs_->setTabIcon(i, UiIcons::icon(QStringLiteral("terminal"), color));
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
    QString label;
    switch (state) {
    case ClaudeActivity::State::Working: label = tr("Claude is working"); break;
    case ClaudeActivity::State::NeedsInput: label = tr("Claude needs you"); break;
    case ClaudeActivity::State::Done: label = tr("Claude has finished"); break;
    case ClaudeActivity::State::Idle: break;
    }
    titleBar_->setClaudeState(icon, label, description + QLatin1Char('\n') + tr("Show the Claude session (Ctrl+Shift+L)"));
    const QString modeHint = tr("Full-screen terminals (Ctrl+1)");
    modeTabs_->setTabToolTip(0, state == ClaudeActivity::State::Idle ? modeHint
                                                                     : description + QLatin1Char('\n') + modeHint);
}

void MainWindow::updateClaudeWatched()
{
    claudeActivity_.setWatched(claudeTerminal_ && isActiveWindow() && mode_ == Mode::Terminal
                               && terminalTabs_->currentWidget() == claudeTerminal_);
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

void MainWindow::snapshotBeforePrompt(const QString &prompt)
{
    if (!snapshotBeforePrompt_->isChecked())
        return;
    QString summary = prompt.section(QLatin1Char('\n'), 0, 0).simplified();
    if (summary.size() > snapshotPromptLength)
        summary = summary.left(snapshotPromptLength - 1) + QStringLiteral("…");
    const QString reason = summary.isEmpty() ? tr("Before a Claude prompt")
                                             : tr("Before the Claude prompt \u201c%1\u201d").arg(summary);
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
    const bool dark = theme_.isDark;
    const double highest = usage.highestPercent();
    const char *warning = highest >= 90 ? (dark ? "#e06c75" : "#e45649") : highest >= 75 ? (dark ? "#e5c07b" : "#c18401") : nullptr;
    usageLabel_->setText(warning ? QStringLiteral("<span style=\"color:%1\">%2</span>")
                                       .arg(QLatin1String(warning), summary.toHtmlEscaped())
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
    if (!claudeTerminal_ || !claudeTerminal_->isRunning()) {
        QString error;
        if (!startClaudeSession(&error)) {
            QMessageBox::warning(this, tr("Claude Session"), error);
            return;
        }
    }
    QString relative = QDir(folder_).relativeFilePath(path);
    if (relative == QLatin1String("."))
        relative = QStringLiteral("./");
    else if (QFileInfo(path).isDir())
        relative += QLatin1Char('/');
    // A file mention in Claude Code's prompt, typed where the cursor is.
    claudeTerminal_->paste(QStringLiteral("@%1 ").arg(QDir::toNativeSeparators(relative)));
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
    QStringList paths;
    if (rollback) {
        // The changed files git knows, under `path`: untracked files stay.
        const FileStatusMap statuses = fileStatuses_->statuses();
        for (auto it = statuses.cbegin(); it != statuses.cend(); ++it) {
            if (it.value() == FileStatus::Untracked || it.value() == FileStatus::Ignored)
                continue;
            if (it.key() == path || it.key().startsWith(path + QLatin1Char('/')))
                paths.append(info->repoPath(folder.relativeFilePath(it.key())));
        }
        if (paths.isEmpty())
            return;
    } else {
        const QString relative = folder.relativeFilePath(path);
        paths.append(relative == QLatin1String(".") ? (info->prefix.isEmpty() ? QStringLiteral(".") : info->prefix)
                                                    : info->repoPath(relative));
    }
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
    EditorWidget *editor = mode_ == Mode::Edit ? editorTabs_->currentEditor() : nullptr;
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
        statusBar()->showMessage(gutter->hasBase() ? tr("No changes in this file")
                                                   : tr("This file is not tracked by git"),
                                 statusMessageMs);
}

void MainWindow::rollbackChange()
{
    EditorWidget *editor = mode_ == Mode::Edit ? editorTabs_->currentEditor() : nullptr;
    if (editor && !editor->changeGutter()->rollbackAtCursor())
        statusBar()->showMessage(tr("No change on this line"), statusMessageMs);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        if (isActiveWindow() && trayIcon_)
            trayIcon_->hide();
        updateClaudeWatched();
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == claudeTerminal_ && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
        claudeActivity_.interrupt();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateRepositoryInfo()
{
    std::optional<RepositoryInfo> info;
    try {
        info = RepositoryInfo::read(folder_);
    } catch (const GitError &) {
        // An unreadable repository: no branch shown.
    }
    repositoryWatcher_->setGitDir(info ? info->gitDir : QString());
    if (inRepository_ != info.has_value()) {
        inRepository_ = info.has_value();
        fileTree_->setStatuses(fileStatuses_->statuses(), inRepository_);
    }
    fileStatuses_->refresh();
    if (!info) {
        branchButton_->hide();
        return;
    }

    QString text = info->detached ? tr("Detached HEAD (%1)").arg(info->headId) : info->branch;
    if (info->unborn)
        text += tr(" — no commits");
    if (!info->operation.isEmpty())
        text += tr(" — %1 in progress").arg(info->operation);
    branchButton_->setText(text);
    branchButton_->setToolTip(info->unborn ? tr("Repository %1").arg(info->workDir)
                                           : tr("Repository %1\nLast commit %2: %3")
                                                 .arg(info->workDir, info->headId, info->headMessage.section(QLatin1Char('\n'), 0, 0)));
    branchButton_->setMenu(gitMenu_);
    branchButton_->show();
}

void MainWindow::onRepositoryChanged()
{
    updateRepositoryInfo();
    symbolIndex_->refresh(); // branch switch, pull…
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
                    TerminalWidget *terminal = addTerminal(title, spec);
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
