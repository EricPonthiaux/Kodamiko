#include "MainWindow.h"

#include "changes/DiffView.h"
#include "claude/ClaudeLauncher.h"
#include "editor/EditorTabs.h"
#include "outline/OutlineView.h"
#include "terminal/TerminalWidget.h"
#include "workspace/FileTreeView.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>

#include <algorithm>

namespace {

constexpr int statusMessageMs = 5000;

QPalette paletteFor(const Theme &theme)
{
    if (theme.name != QLatin1String("dark"))
        return QApplication::style()->standardPalette();
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#21252b"));
    palette.setColor(QPalette::WindowText, theme.foreground);
    palette.setColor(QPalette::Base, theme.background);
    palette.setColor(QPalette::AlternateBase, QColor("#262a31"));
    palette.setColor(QPalette::Text, theme.foreground);
    palette.setColor(QPalette::Button, QColor("#2c313a"));
    palette.setColor(QPalette::ButtonText, theme.foreground);
    palette.setColor(QPalette::Highlight, QColor("#3e5f8a"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor("#2c313a"));
    palette.setColor(QPalette::ToolTipText, theme.foreground);
    palette.setColor(QPalette::PlaceholderText, theme.marginForeground);
    palette.setColor(QPalette::Disabled, QPalette::Text, theme.marginForeground);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.marginForeground);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.marginForeground);
    return palette;
}

} // namespace

MainWindow::MainWindow(const QString &folder, QWidget *parent)
    : QMainWindow(parent)
    , theme_(Theme::dark())
{
    createWidgets();
    createMenus();
    restoreSettings();

    connect(&hookServer_, &HookServer::hookReceived, this, &MainWindow::onHookReceived);
    if (!hookServer_.listen())
        statusBar()->showMessage(tr("Suivi de Claude indisponible : %1").arg(hookServer_.errorString()));

    const QString lastFolder = QSettings().value(QStringLiteral("lastFolder")).toString();
    openFolder(!folder.isEmpty() ? folder : !lastFolder.isEmpty() ? lastFolder : QDir::homePath());
    setMode(static_cast<Mode>(
        std::clamp(QSettings().value(QStringLiteral("viewMode"), static_cast<int>(Mode::Edit)).toInt(), 0, 2)));
}

MainWindow::~MainWindow() = default;

void MainWindow::createWidgets()
{
    editorTabs_ = new EditorTabs(theme_, this);
    connect(editorTabs_, &EditorTabs::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, statusMessageMs); });

    diffView_ = new DiffView(theme_, this);
    connect(diffView_, &DiffView::openFileRequested, this, [this](const QString &relativePath) {
        setMode(Mode::Edit);
        editorTabs_->openFile(absolutePath(relativePath));
    });

    // Même ordre que l'énumération Mode.
    modes_ = new QStackedWidget(this);
    modes_->addWidget(createTerminalPage());
    modes_->addWidget(createEditPage());
    modes_->addWidget(diffView_);
    setCentralWidget(modes_);
    createModeTabs();

    resize(1400, 900);
}

// Mode Edit : panneau latéral (onglets Fichiers / Structure) | onglets d'éditeur.
QWidget *MainWindow::createEditPage()
{
    fileTree_ = new FileTreeView(this);
    connect(fileTree_, &FileTreeView::fileActivated, editorTabs_, &EditorTabs::openFile);

    outlineView_ = new OutlineView(this);
    connect(editorTabs_, &QTabWidget::currentChanged, this,
            [this] { outlineView_->setEditor(editorTabs_->currentEditor()); });

    sidebar_ = new QTabWidget(this);
    sidebar_->setDocumentMode(true);
    sidebar_->addTab(fileTree_, tr("Fichiers"));
    sidebar_->addTab(outlineView_, tr("Structure"));

    editSplitter_ = new QSplitter(Qt::Horizontal, this);
    editSplitter_->setChildrenCollapsible(false);
    editSplitter_->addWidget(sidebar_);
    editSplitter_->addWidget(editorTabs_);
    editSplitter_->setStretchFactor(1, 1);
    editSplitter_->setSizes({260, 1140});
    return editSplitter_;
}

// Mode Terminal : les onglets de terminaux, ou une aide tant qu'il n'y en a pas.
QWidget *MainWindow::createTerminalPage()
{
    terminalTabs_ = new QTabWidget(this);
    terminalTabs_->setTabsClosable(true);
    terminalTabs_->setDocumentMode(true);
    connect(terminalTabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget *terminal = terminalTabs_->widget(index);
        terminalTabs_->removeTab(index);
        terminal->deleteLater();
        updateTerminalPage();
    });

    auto *placeholder = new QLabel(tr("Aucun terminal ouvert\n\n"
                                      "Ctrl+Maj+L : nouvelle session Claude\n"
                                      "Ctrl+Maj+T : nouveau terminal"),
                                   this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setEnabled(false);

    terminalPage_ = new QStackedWidget(this);
    terminalPage_->addWidget(placeholder);
    terminalPage_->addWidget(terminalTabs_);
    return terminalPage_;
}

// Onglets Terminal | Edit | Diff en bas à droite, dans la barre d'état.
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
    modeTabs_->setTabToolTip(0, tr("Terminaux en plein écran (Ctrl+1)"));
    modeTabs_->setTabToolTip(1, tr("Arborescence et éditeur de code (Ctrl+2)"));
    modeTabs_->setTabToolTip(2, tr("Changements en cours sur tous les fichiers (Ctrl+3)"));
    connect(modeTabs_, &QTabBar::currentChanged, this, [this](int index) { setMode(static_cast<Mode>(index)); });

    // Les messages de la barre d'état restent affichés à gauche des onglets.
    modeTabs_->setShape(QTabBar::RoundedSouth);
    statusBar()->addPermanentWidget(modeTabs_);
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&Fichier"));
    file->addAction(tr("Ouvrir un &dossier…"), QKeySequence(tr("Ctrl+K, Ctrl+O")), this, &MainWindow::chooseFolder);
    file->addAction(tr("&Ouvrir un fichier…"), QKeySequence::Open, this, &MainWindow::chooseFile);
    file->addSeparator();
    file->addAction(tr("&Enregistrer"), QKeySequence::Save, editorTabs_, &EditorTabs::saveCurrent);
    file->addAction(tr("Enregistrer &tout"), QKeySequence(tr("Ctrl+Alt+S")), editorTabs_, &EditorTabs::saveAll);
    file->addAction(tr("&Fermer l'onglet"), QKeySequence::Close, editorTabs_, &EditorTabs::closeCurrent);
    file->addSeparator();
    file->addAction(tr("&Quitter"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *claude = menuBar()->addMenu(tr("&Claude"));
    claude->addAction(tr("Nouvelle session &Claude"), QKeySequence(tr("Ctrl+Shift+L")), this,
                      &MainWindow::newClaudeSession);
    claude->addAction(tr("Nouveau &terminal"), QKeySequence(tr("Ctrl+Shift+T")), this, &MainWindow::newShell);

    QMenu *view = menuBar()->addMenu(tr("&Affichage"));
    QAction *darkTheme = view->addAction(tr("Thème &sombre"));
    darkTheme->setCheckable(true);
    darkTheme->setChecked(QSettings().value(QStringLiteral("darkTheme"), true).toBool());
    connect(darkTheme, &QAction::toggled, this, &MainWindow::setDarkTheme);
    view->addSeparator();

    // Ctrl+1/2/3 restent actifs depuis un terminal (voir TerminalWidget::event).
    modeActions_ = new QActionGroup(this);
    const QStringList modeNames{tr("Mode &Terminal"), tr("Mode &Edit"), tr("Mode &Diff")};
    for (int i = 0; i < modeNames.size(); ++i) {
        QAction *action = view->addAction(modeNames[i]);
        action->setCheckable(true);
        action->setShortcut(QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + i)));
        action->setShortcutContext(Qt::ApplicationShortcut);
        modeActions_->addAction(action);
        connect(action, &QAction::triggered, this, [this, i] { setMode(static_cast<Mode>(i)); });
    }
    modeActions_->actions().at(static_cast<int>(mode_))->setChecked(true);
    QAction *toggleTerminal = view->addAction(tr("Basculer vers le terminal"));
    toggleTerminal->setShortcut(QKeySequence(Qt::Key_F11));
    toggleTerminal->setShortcutContext(Qt::ApplicationShortcut);
    connect(toggleTerminal, &QAction::triggered, this, &MainWindow::toggleTerminalMode);
    view->addSeparator();
    view->addAction(tr("S&tructure du fichier"), QKeySequence(tr("Ctrl+Shift+O")), this, &MainWindow::showOutline);

    const auto addPanelToggle = [&](const QString &name, QWidget *panel) {
        QAction *action = view->addAction(name);
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::toggled, panel, &QWidget::setVisible);
        return action;
    };
    QSettings settings;
    showSidebar_ = addPanelToggle(tr("&Panneau latéral"), sidebar_);
    showSidebar_->setChecked(settings.value(QStringLiteral("showSidebar"), true).toBool());
}

void MainWindow::restoreSettings()
{
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    editSplitter_->restoreState(settings.value(QStringLiteral("editSplitter")).toByteArray());
    diffView_->splitter()->restoreState(settings.value(QStringLiteral("diffSplitter")).toByteArray());
    sidebar_->setCurrentIndex(settings.value(QStringLiteral("sidebarTab"), 0).toInt());
    setDarkTheme(settings.value(QStringLiteral("darkTheme"), true).toBool());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!editorTabs_->closeAll()) {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("editSplitter"), editSplitter_->saveState());
    settings.setValue(QStringLiteral("diffSplitter"), diffView_->splitter()->saveState());
    settings.setValue(QStringLiteral("showSidebar"), !sidebar_->isHidden());
    settings.setValue(QStringLiteral("sidebarTab"), sidebar_->currentIndex());
    settings.setValue(QStringLiteral("viewMode"), static_cast<int>(mode_));
    settings.setValue(QStringLiteral("lastFolder"), folder_);
    event->accept();
}

void MainWindow::openFolder(const QString &folder)
{
    folder_ = QDir(folder).absolutePath();
    fileTree_->setRootPath(folder_);
    diffView_->setFolder(folder_);
    if (mode_ == Mode::Diff)
        diffView_->refresh();
    setWindowTitle(QStringLiteral("%1 — Enlumine").arg(QFileInfo(folder_).fileName()));
}

void MainWindow::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Ouvrir un dossier"), folder_);
    if (!folder.isEmpty())
        openFolder(folder);
}

void MainWindow::chooseFile()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Ouvrir des fichiers"), folder_);
    for (const QString &file : files)
        editorTabs_->openFile(file);
}

void MainWindow::newClaudeSession()
{
    QString error;
    const std::optional<ProcessSpec> spec = ClaudeLauncher::claudeProcess(folder_, hookServer_.serverName(), &error);
    if (!spec) {
        QMessageBox::warning(this, tr("Session Claude"), error);
        return;
    }
    addTerminal(QStringLiteral("Claude"), *spec);
}

void MainWindow::newShell()
{
    addTerminal(tr("Terminal"), defaultShell(folder_));
}

void MainWindow::addTerminal(const QString &title, const ProcessSpec &spec)
{
    auto *terminal = new TerminalWidget(terminalTabs_);
    terminal->applyTheme(theme_);
    const int index = terminalTabs_->addTab(terminal, title);
    terminalTabs_->setCurrentIndex(index);
    updateTerminalPage();
    setMode(Mode::Terminal);
    terminal->start(spec);
    terminal->setFocus();
}

void MainWindow::setDarkTheme(bool dark)
{
    theme_ = dark ? Theme::dark() : Theme::light();
    QApplication::setPalette(paletteFor(theme_));
    editorTabs_->applyTheme(theme_);
    for (int i = 0; i < terminalTabs_->count(); ++i) {
        if (auto *terminal = qobject_cast<TerminalWidget *>(terminalTabs_->widget(i)))
            terminal->applyTheme(theme_);
    }
    diffView_->applyTheme(theme_);
    QSettings().setValue(QStringLiteral("darkTheme"), dark);
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

    switch (mode) {
    case Mode::Diff:
        diffView_->refresh();
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
}

// F11 : vers le terminal, puis retour au mode précédent.
void MainWindow::toggleTerminalMode()
{
    setMode(mode_ == Mode::Terminal ? modeBeforeTerminal_ : Mode::Terminal);
}

// Ctrl+Maj+O : volet Structure, le filtre prêt à la saisie.
void MainWindow::showOutline()
{
    setMode(Mode::Edit);
    showSidebar_->setChecked(true);
    sidebar_->setCurrentWidget(outlineView_);
    outlineView_->focusFilter();
}

void MainWindow::updateTerminalPage()
{
    terminalPage_->setCurrentIndex(terminalTabs_->count() > 0 ? 1 : 0);
}

void MainWindow::onHookReceived(const HookEvent &event)
{
    switch (event.kind) {
    case HookEvent::Kind::FilesEdited:
        for (const QString &path : event.filePaths)
            editorTabs_->syncWithDisk(path);
        if (mode_ == Mode::Diff)
            diffView_->refresh();
        break;
    case HookEvent::Kind::ResponseFinished:
        if (mode_ == Mode::Diff)
            diffView_->refresh();
        statusBar()->showMessage(tr("Réponse de Claude terminée"), statusMessageMs);
        break;
    }
}

QString MainWindow::absolutePath(const QString &relativePath) const
{
    return QDir(folder_).filePath(relativePath);
}
