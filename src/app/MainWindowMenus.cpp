// SPDX-License-Identifier: GPL-3.0-only

// MainWindow: the menu bar and the shortcuts of its commands.

#include "MainWindow.h"
#include "changes/ChangesView.h"
#include "claude/ClaudeConfigView.h"
#include "claude/mcp/DuetTools.h"
#include "completion/CompletionController.h"
#include "diagnostics/ProblemsView.h"
#include "editor/EditorStatus.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/FindBar.h"
#include "navigation/CodeIntelligence.h"
#include "navigation/CodeNavigator.h"
#include "navigation/GoToPopup.h"
#include "projects/ProjectStore.h"
#include "settings/Keymap.h"
#include "settings/Preferences.h"
#include "terminal/TerminalWidget.h"
#include "ui/TitleBar.h"
#include "viewers/DiagramKinds.h"
#include "workspace/CommandPalette.h"
#include "workspace/FileTreeView.h"
#include "workspace/SidePanel.h"

#include <QActionGroup>
#include <QDir>
#include <QFileInfo>
#include <QMenuBar>
#include <QSettings>
#include <QTreeWidget>

#include <algorithm>

void MainWindow::createMenus()
{
    createFileMenu();
    createEditMenu();
    createClaudeMenu();
    createNavigateMenu();
    createRunMenu();
    createGitMenu();
    createViewMenu();
    createHelpMenu();

    // Actions at the right of the title bar.
    titleBar_->setRunActions(runAction_, stopAction_);
    titleBar_->addActionButton(goToFileAction_, QStringLiteral("find-file"));
    titleBar_->addActionButton(findInFilesAction_, QStringLiteral("find-in-folder"));
    titleBar_->addActionButton(paletteAction_, QStringLiteral("command"));
}

void MainWindow::createFileMenu()
{
    QMenu *file = titleBar_->menuBar()->addMenu(tr("&File"));
    file->addAction(tr("New &Project…"), this, &MainWindow::showNewProject);
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
}

void MainWindow::createClaudeMenu()
{
    QMenu *claude = titleBar_->menuBar()->addMenu(tr("&Claude"));
    claude->addAction(tr("&Claude Session"), QKeySequence(tr("Ctrl+Shift+L")), this, &MainWindow::showClaudeSession);
    claude->addAction(tr("New Claude Session in &Worktree…"), this, &MainWindow::newAgentSession);
    claude->addAction(tr("&Merge Agent's Work"), this, &MainWindow::mergeCurrentAgent);
    claude->addAction(tr("Chec&k Before Merging Agents…"), this, &MainWindow::editAgentCheckCommand);
    if (!DiagramKinds::available().isEmpty())
        claude->addAction(tr("&Draw a Diagram…"), this, [this] { fileTree_->generateDiagram(); });
    claude->addSeparator();
    createCodeRequestActions(claude);
    claude->addSeparator();
    claude->addAction(tr("Restore the State Before a &Prompt…"), this, &MainWindow::showSnapshots);
    snapshotBeforePrompt_ = addSettingToggle(
        claude, {tr("&Snapshot Before Each Prompt"), QStringLiteral("snapshotBeforePrompt"),
                 tr("Keep a copy of the working directory before Claude works on a prompt, to undo its changes")});
    shareEditorContext_ = addSettingToggle(
        claude, {tr("Share &Editor Context With Each Prompt"), QStringLiteral("shareEditorContext"),
                 tr("Tell Claude the open file, the cursor, the selection, the problems shown and the unsaved files")});
    exposeTools_ = addSettingToggle(
        claude, {tr("E&xpose Duet's Tools to Claude"), QStringLiteral("exposeTools"),
                 tr("Let Claude read and act in the editor through Duet's MCP server (next session)")});
    claudeNotifications_ =
        addSettingToggle(claude, {tr("&Notifications"), QStringLiteral("claudeNotifications"),
                                  tr("Alert when Claude needs you or has finished while you are elsewhere")});
}

QAction *MainWindow::addSettingToggle(QMenu *menu, const SettingToggle &toggle)
{
    QAction *action = menu->addAction(toggle.text);
    action->setCheckable(true);
    action->setChecked(QSettings().value(toggle.key, true).toBool());
    action->setToolTip(toggle.toolTip);
    connect(action, &QAction::toggled, this, [key = toggle.key](bool checked) { QSettings().setValue(key, checked); });
    return action;
}

void MainWindow::createViewMenu()
{
    QMenu *view = titleBar_->menuBar()->addMenu(tr("&View"));
    paletteAction_ = view->addAction(tr("&Command Palette…"), this, &MainWindow::showCommandPalette);
    paletteAction_->setShortcut(QKeySequence(tr("Ctrl+Shift+P")));
    paletteAction_->setShortcutContext(Qt::ApplicationShortcut);
    view->addSeparator();
    QMenu *themes = view->addMenu(tr("T&heme"));
    auto *themeGroup = new QActionGroup(themes);
    themeActions_ = themeGroup;
    // Duet's and KDE's in one list, by name.
    QList<Theme> sortedThemes = Theme::all();
    std::ranges::sort(sortedThemes, [](const Theme &a, const Theme &b) {
        return QString::compare(a.title, b.title, Qt::CaseInsensitive) < 0;
    });
    for (const Theme &theme : sortedThemes) {
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
    view->addAction(tr("&New Terminal"), QKeySequence(tr("Ctrl+Shift+T")), this, &MainWindow::newShell);
    detachTerminalsAction_ = view->addAction(tr("Terminals in a Separate &Window"));
    detachTerminalsAction_->setCheckable(true);
    detachTerminalsAction_->setToolTip(tr("The terminal tabs in a window of their own, to put on another screen"));
    connect(detachTerminalsAction_, &QAction::toggled, this, &MainWindow::setTerminalsDetached);
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
    showSidebar_->setChecked(QSettings().value(QStringLiteral("showSidebar"), true).toBool());

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

void MainWindow::applyKeymap(const Keymap::Overrides &overrides)
{
    const QList<Keymap::Command> commands = Keymap::commands(titleBar_->menuBar());
    Keymap::apply(commands, overrides);
    EditorWidget::setReservedKeys(Keymap::shortcutsInUse(commands));
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
    projectMenu_->addAction(tr("New Project…"), this, &MainWindow::showNewProject);
    projectMenu_->addAction(tr("Open Folder…"), QKeySequence(tr("Ctrl+K, Ctrl+O")), this, &MainWindow::chooseFolder);
    projectMenu_->addAction(tr("Close Project"), this, &MainWindow::closeProject);
    titleBar_->setProject(QFileInfo(folder_).fileName(), QDir::toNativeSeparators(folder_), projectMenu_);
}

void MainWindow::showCommandPalette()
{
    commandPalette_->popup(titleBar_->menuBar());
}
