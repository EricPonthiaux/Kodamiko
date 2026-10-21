// SPDX-License-Identifier: GPL-3.0-only

// MainWindow: the menu bar and the shortcuts of its commands.

#include "MainWindow.h"
#include "agent/mcp/DuetTools.h"
#include "agent/vendor/claude/ClaudeConfigView.h"
#include "changes/ChangesView.h"
#include "completion/CompletionController.h"
#include "diagnostics/ProblemsView.h"
#include "editor/EditorStatus.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/FindBar.h"
#include "navigation/CodeIntelligence.h"
#include "navigation/CodeNavigator.h"
#include "navigation/GoToPopup.h"
#include "navigation/HierarchyView.h"
#include "navigation/UsagesView.h"
#include "projects/ProjectStore.h"
#include "refactor/RefactoringHistory.h"
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
#include <functional>

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
    createEditorContextActions();

    // Actions at the right of the title bar.
    titleBar_->setRunActions(runAction_, stopAction_);
    titleBar_->addActionButton(goToFileAction_, QStringLiteral("find-file"));
    titleBar_->addActionButton(findInFilesAction_, QStringLiteral("find-in-folder"));
    titleBar_->addActionButton(paletteAction_, QStringLiteral("command"));
}

void MainWindow::createEditorContextActions()
{
    const auto separator = [this] {
        auto *action = new QAction(this);
        action->setSeparator(true);
        return action;
    };
    // The requests to the agent in a submenu named after it.
    auto *agent = new QMenu(this);
    agent->addActions(codeRequestActions_);
    nameAfterAgent(agent->menuAction(), tr("%1"));
    editorContextActions_ = refactoringActions_;
    editorContextActions_ << separator();
    editorContextActions_ << navigationActions_;
    editorContextActions_ << separator() << agent->menuAction();
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
        drawDiagramActions_.append(nameAfterAgent(
            diagram->addAction(QString(), this, [this] { fileTree_->generateDiagram(); }), tr("Drawn by &%1…")));
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
    // Named after the project's agent: "Claude", "Claude Session"…
    QMenu *claude = titleBar_->menuBar()->addMenu(QString());
    nameAfterAgent(claude->menuAction(), tr("&%1"));
    nameAfterAgent(claude->addAction(QString(), QKeySequence(tr("Ctrl+Shift+L")), this, &MainWindow::showClaudeSession),
                   tr("&%1 Session"));
    nameAfterAgent(claude->addAction(QString(), this, &MainWindow::newAgentSession),
                   tr("New %1 Session in &Worktree…"));
    claude->addAction(tr("&Merge Agent's Work"), this, &MainWindow::mergeCurrentAgent);
    claude->addAction(tr("Chec&k Before Merging Agents…"), this, &MainWindow::editAgentCheckCommand);
    if (!DiagramKinds::available().isEmpty())
        drawDiagramActions_.append(
            claude->addAction(tr("&Draw a Diagram…"), this, [this] { fileTree_->generateDiagram(); }));
    claude->addSeparator();
    createCodeRequestActions(claude);
    claude->addSeparator();
    claude->addAction(tr("Restore the State Before a &Prompt…"), this, &MainWindow::showSnapshots);
    snapshotBeforePrompt_ = addAgentSettingToggle(
        claude, {tr("&Snapshot Before Each Prompt"), QStringLiteral("snapshotBeforePrompt"),
                 tr("Keep a copy of the working directory before %1 works on a prompt, to undo its changes")});
    shareEditorContext_ = addAgentSettingToggle(
        claude, {tr("Share &Editor Context With Each Prompt"), QStringLiteral("shareEditorContext"),
                 tr("Tell %1 the open file, the cursor, the selection, the problems shown and the unsaved files")});
    exposeTools_ = addAgentSettingToggle(
        claude, {tr("E&xpose Duet's Tools to %1"), QStringLiteral("exposeTools"),
                 tr("Let %1 read and act in the editor through Duet's MCP server (next session)")});
    claudeNotifications_ =
        addAgentSettingToggle(claude, {tr("&Notifications"), QStringLiteral("claudeNotifications"),
                                       tr("Alert when %1 needs you or has finished while you are elsewhere")});
    claude->addSeparator();
    createAgentProviderMenu(claude);
}

QAction *MainWindow::addAgentSettingToggle(QMenu *menu, const SettingToggle &toggle)
{
    return nameAfterAgent(addSettingToggle(menu, toggle), toggle.text, toggle.toolTip);
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
    view->addAction(tr("&Usages"), QKeySequence(tr("Alt+3")), this, [this] {
        setMode(Mode::Edit);
        sidebar_->showView(usagesView_);
        usagesView_->tree()->setFocus();
    });
    view->addAction(tr("Hie&rarchy"), QKeySequence(tr("Alt+8")), this, [this] {
        setMode(Mode::Edit);
        sidebar_->showView(hierarchyView_);
        hierarchyView_->tree()->setFocus();
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
    refactoringActions_.append(edit->addAction(tr("Refactor &This…"), QKeySequence(tr("Ctrl+Alt+Shift+T")), this,
                                               inCode(&CodeIntelligence::refactorThis)));
    refactoringActions_.append(
        edit->addAction(tr("Re&name…"), QKeySequence(tr("Shift+F6")), this, inCode(&CodeIntelligence::rename)));
    refactoringActions_.append(edit->addAction(tr("Change Si&gnature…"), QKeySequence(tr("Ctrl+F6")), this,
                                               inCode(&CodeIntelligence::changeSignature)));
    refactoringActions_.append(edit->addAction(tr("Move C&lass to Its Own File"), QKeySequence(tr("F6")), this,
                                               inCode(&CodeIntelligence::moveClassToOwnFile)));
    refactoringActions_.append(edit->addAction(tr("Show Quick F&ixes…"), QKeySequence(tr("Alt+Return")), this,
                                               inCode(&CodeIntelligence::showQuickFixes)));
    // In every file the refactoring changed, open or not.
    QAction *undoRefactoring = edit->addAction(tr("&Undo Refactoring"), QKeySequence(tr("Ctrl+Alt+Shift+Z")), this,
                                               inCode(&CodeIntelligence::undoRefactoring));
    Keymap::setStableName(undoRefactoring, QStringLiteral("Undo Refactoring")); // its text names the refactoring
    const auto updateUndoRefactoring = [this, undoRefactoring] {
        const QString last = intelligence_->history()->lastDescription();
        undoRefactoring->setEnabled(!last.isEmpty());
        undoRefactoring->setText(last.isEmpty() ? tr("&Undo Refactoring") : tr("&Undo %1").arg(last));
    };
    connect(intelligence_->history(), &RefactoringHistory::changed, this, updateUndoRefactoring);
    updateUndoRefactoring();
    refactoringActions_.append(undoRefactoring);
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
    // The code commands do nothing outside the Edit mode.
    const auto inEditMode = [this](const std::function<void()> &command) {
        return [this, command] {
            if (mode_ == Mode::Edit)
                command();
        };
    };
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
    navigationActions_.append(navigate->addAction(tr("Go to &Definition"), QKeySequence(tr("Ctrl+B")), this,
                                                  inEditMode([this] { intelligence_->goToDefinition(); })));
    // Ctrl+Alt+B, IntelliJ's, opens the branches.
    navigationActions_.append(navigate->addAction(tr("Go to &Implementation"), QKeySequence(tr("Ctrl+Alt+Shift+B")),
                                                  this, inEditMode([this] { intelligence_->goToImplementation(); })));
    navigationActions_.append(navigate->addAction(tr("Go to T&ype Declaration"), QKeySequence(tr("Ctrl+Shift+B")), this,
                                                  inEditMode([this] { intelligence_->goToTypeDeclaration(); })));
    navigationActions_.append(navigate->addAction(tr("Find &Usages"), QKeySequence(tr("Alt+F7")), this,
                                                  inEditMode([this] { intelligence_->findUsages(); })));
    navigationActions_.append(navigate->addAction(tr("&Call Hierarchy"), QKeySequence(tr("Ctrl+Alt+H")), this,
                                                  inEditMode([this] { intelligence_->showCallHierarchy(); })));
    navigationActions_.append(navigate->addAction(tr("Type Hi&erarchy"), QKeySequence(tr("Ctrl+H")), this,
                                                  inEditMode([this] { intelligence_->showTypeHierarchy(); })));
    navigate->addAction(tr("Ne&xt Usage"), QKeySequence(tr("Ctrl+Alt+Down")), this,
                        inEditMode([this] { usagesView_->goToNext(); }));
    navigate->addAction(tr("Pre&vious Usage"), QKeySequence(tr("Ctrl+Alt+Up")), this,
                        inEditMode([this] { usagesView_->goToPrevious(); }));
    navigate->addAction(tr("Switch &Header/Source"), QKeySequence(tr("Ctrl+Alt+Home")), this,
                        inEditMode([this] { intelligence_->switchHeaderSource(); }));
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
        QAction *action = projectMenu_->addAction(QFileInfo(folder).fileName(), this,
                                                  [this, folder] { askToOpenProject(folder, AfterOpening::Nothing); });
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
