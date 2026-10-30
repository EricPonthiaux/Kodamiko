// SPDX-License-Identifier: GPL-3.0-only

// MainWindow: the menu bar and the shortcuts of its commands.

#include "MainWindow.h"
#include "agent/mcp/KDKTools.h"
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
#include "system/Host.h"
#include "terminal/TerminalWidget.h"
#include "ui/AboutPopup.h"
#include "ui/DesktopIntegration.h"
#include "ui/FileIcons.h"
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
#include <QTabBar>
#include <QTreeWidget>

#include <algorithm>
#include <array>
#include <functional>

namespace {

// The other projects that the Projects and Dock menus list.
constexpr int recentProjectCount = 10;

// macOS's own full-screen key; F11, the others', switches to the terminal.
QKeySequence fullScreenShortcut()
{
    switch (Host::operatingSystem()) {
    case OperatingSystem::MacOS:
        return QKeySequence(Qt::META | Qt::CTRL | Qt::Key_F); // ⌃⌘F
    case OperatingSystem::Windows:
    case OperatingSystem::Linux:
        return QKeySequence(Qt::SHIFT | Qt::Key_F11);
    }
    return {};
}

} // namespace

void MainWindow::createMenus()
{
    const QList<QAction *> fileApplicationActions = createFileMenu();
    createEditMenu();
    createClaudeMenu();
    createNavigateMenu();
    createRunMenu();
    createGitMenu();
    createViewMenu();
    // About first, where macOS applications have it.
    nativeMenuBar_ =
        DesktopIntegration::createNativeMenuBar(QList<QAction *>{createHelpMenu()} + fileApplicationActions);
    createEditorContextActions();
    createDockMenu();

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
    editorContextActions_ = {renameAction_, findUsagesAction_, agent->menuAction(), separator()};
    editorContextActions_ << navigationActions_;
    editorContextActions_ << separator() << quickFixesAction_ << undoRefactoringAction_;
}

QList<QAction *> MainWindow::createFileMenu()
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
    QAction *quit = file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    quit->setMenuRole(QAction::QuitRole);
    return {preferences, quit};
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
    claude->addAction(tr("Review Staged &Changes"), this, &MainWindow::reviewStagedChanges)
        ->setToolTip(tr("Have a separate session review the staged changes, its findings shown in the diff"));
    QAction *reviewStep = claude->addAction(tr("Review &Step Before the Diff"));
    reviewStep->setCheckable(true);
    reviewStep->setChecked(isReviewStepOn());
    reviewStep->setToolTip(tr("A Review mode between Edit and Diff, where the staged changes are reviewed before "
                              "the diff and the commit"));
    connect(reviewStep, &QAction::toggled, this, &MainWindow::setReviewStep);
    QMenu *reviewerModel = claude->addMenu(tr("Reviewer &Model"));
    connect(reviewerModel, &QMenu::aboutToShow, this, [this, reviewerModel] { fillReviewerModelMenu(reviewerModel); });
    claude->addSeparator();
    claude->addAction(tr("Restore the State Before a &Prompt…"), this, &MainWindow::showSnapshots);
    snapshotBeforePrompt_ = addAgentSettingToggle(
        claude, {tr("&Snapshot Before Each Prompt"), QStringLiteral("snapshotBeforePrompt"),
                 tr("Keep a copy of the working directory before %1 works on a prompt, to undo its changes")});
    shareEditorContext_ = addAgentSettingToggle(
        claude, {tr("Share &Editor Context With Each Prompt"), QStringLiteral("shareEditorContext"),
                 tr("Tell %1 the open file, the cursor, the selection, the problems shown and the unsaved files")});
    reportEditErrors_ = addAgentSettingToggle(
        claude, {tr("Report E&rrors After Each Edit"), QStringLiteral("reportEditErrors"),
                 tr("Tell %1 the errors the language servers find in the code it has just edited")});
    exposeTools_ = addAgentSettingToggle(
        claude, {tr("E&xpose Kodamiko's Tools to %1"), QStringLiteral("exposeTools"),
                 tr("Let %1 read and act in the editor through Kodamiko's MCP server (next session)")});
    gitThroughKDKTools_ = addAgentSettingToggle(
        claude, {tr("Send &Git Commands to Kodamiko's Tools"), QStringLiteral("gitThroughKDKTools"),
                 tr("Refuse the git commands %1 runs in its shell to change the repository when a Kodamiko tool does "
                    "the same: the tools ask you first and take snapshots")});
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
    // Kodamiko's and KDE's in one list, by name.
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

    // Ctrl+1 to 5 remain active from a terminal (see TerminalWidget::event).
    // In the order of Mode; Review keeps the key after the others, which
    // kept theirs when it came.
    struct ModeAction {
        QString name;
        Qt::Key key;
    };

    const std::array<ModeAction, 5> modeActions{{{tr("&Terminal Mode"), Qt::Key_1},
                                                 {tr("&Edit Mode"), Qt::Key_2},
                                                 {tr("&Review Mode"), Qt::Key_5},
                                                 {tr("&Diff Mode"), Qt::Key_3},
                                                 {tr("&History Mode"), Qt::Key_4}}};
    modeActions_ = new QActionGroup(this);
    for (int i = 0; i < int(modeActions.size()); ++i) {
        QAction *action = view->addAction(modeActions.at(i).name);
        action->setCheckable(true);
        action->setShortcut(QKeySequence(Qt::CTRL | modeActions.at(i).key));
        action->setShortcutContext(Qt::ApplicationShortcut);
        modeActions_->addAction(action);
        connect(action, &QAction::triggered, this, [this, i] { setMode(static_cast<Mode>(i)); });
    }
    modeActions_->actions().at(int(Mode::Review))->setVisible(isReviewStepOn());
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
    fullScreenAction_ = view->addAction(tr("&Full Screen"), fullScreenShortcut(), this, &MainWindow::toggleFullScreen);
    fullScreenAction_->setCheckable(true);
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

    // Also folded by clicking its current tab, or by double-clicking an
    // editor tab: the editor takes the whole width, as in VS Code.
    showSidebar_ = view->addAction(tr("&Side Panel"));
    showSidebar_->setCheckable(true);
    showSidebar_->setChecked(true);
    connect(showSidebar_, &QAction::toggled, sidebar_, &SidePanel::setPanelVisible);
    connect(sidebar_, &SidePanel::panelVisibilityChanged, showSidebar_, &QAction::setChecked);
    connect(editorTabs_->tabBar(), &QTabBar::tabBarDoubleClicked, showSidebar_, [this](int index) {
        if (index >= 0)
            showSidebar_->toggle();
    });
    showSidebar_->setChecked(QSettings().value(QStringLiteral("showSidebar"), true).toBool());
    sidebarAtRight_ = view->addAction(tr("Side Panel at the &Right"));
    sidebarAtRight_->setCheckable(true);
    sidebarAtRight_->setChecked(sidebar_->edge() == SidePanel::Edge::Right);
    connect(sidebarAtRight_, &QAction::toggled, this, [this](bool atRight) {
        placeSidebar(atRight ? SidePanel::Edge::Right : SidePanel::Edge::Left);
        QSettings().setValue(QStringLiteral("sidebarAtRight"), atRight);
    });

    view->addSeparator();
    systemTitleBar_ = view->addAction(tr("Use System &Title Bar"));
    systemTitleBar_->setCheckable(true);
    systemTitleBar_->setToolTip(tr("The system's window frame instead of Kodamiko's (tiling window managers…)"));
    connect(systemTitleBar_, &QAction::toggled, this, [this](bool system) {
        setSystemTitleBar(system);
        QSettings().setValue(QStringLiteral("systemTitleBar"), system);
    });
    // macOS keeps its own frame and traffic lights.
    systemTitleBar_->setVisible(Host::operatingSystem() != OperatingSystem::MacOS);
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
    renameAction_ =
        edit->addAction(tr("Re&name…"), QKeySequence(tr("Shift+F6")), this, inCode(&CodeIntelligence::rename));
    edit->addAction(tr("Rename &File…"), this, &MainWindow::renameCurrentFile);
    edit->addAction(tr("Change Si&gnature…"), QKeySequence(tr("Ctrl+F6")), this,
                    inCode(&CodeIntelligence::changeSignature));
    edit->addAction(tr("Move C&lass to Its Own File"), QKeySequence(tr("F6")), this,
                    inCode(&CodeIntelligence::moveClassToOwnFile));
    quickFixesAction_ = edit->addAction(tr("Show Quick F&ixes…"), QKeySequence(tr("Alt+Return")), this,
                                        inCode(&CodeIntelligence::showQuickFixes));
    // In every file the refactoring changed, open or not.
    QAction *undoRefactoring = edit->addAction(tr("&Undo Refactoring"), QKeySequence(tr("Ctrl+Alt+Shift+Z")), this,
                                               inCode(&CodeIntelligence::undoRefactoring));
    Keymap::setStableName(undoRefactoring, QStringLiteral("Undo Refactoring")); // its text names the refactoring
    const auto updateUndoRefactoring = [this, undoRefactoring] {
        const QString last = intelligence_->history()->lastDescription();
        undoRefactoring->setEnabled(!last.isEmpty());
        undoRefactoring->setVisible(!last.isEmpty());
        undoRefactoring->setText(last.isEmpty() ? tr("&Undo Refactoring") : tr("&Undo %1").arg(last));
    };
    connect(intelligence_->history(), &RefactoringHistory::changed, this, updateUndoRefactoring);
    updateUndoRefactoring();
    undoRefactoringAction_ = undoRefactoring;
    QAction *redoRefactoring =
        edit->addAction(tr("&Redo Refactoring"), this, inCode(&CodeIntelligence::redoRefactoring));
    Keymap::setStableName(redoRefactoring, QStringLiteral("Redo Refactoring"));
    const auto updateRedoRefactoring = [this, redoRefactoring] {
        const QString next = intelligence_->history()->nextRedoDescription();
        redoRefactoring->setEnabled(!next.isEmpty());
        redoRefactoring->setVisible(!next.isEmpty());
        redoRefactoring->setText(next.isEmpty() ? tr("&Redo Refactoring") : tr("&Redo %1").arg(next));
    };
    connect(intelligence_->history(), &RefactoringHistory::changed, this, updateRedoRefactoring);
    updateRedoRefactoring();
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

void MainWindow::renameCurrentFile()
{
    // In the tree, which updates the includes and imports.
    const EditorWidget *editor = editorInEditMode();
    if (editor && !editor->filePath().isEmpty())
        fileTree_->renameEntry(editor->filePath());
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
    findUsagesAction_ = navigate->addAction(tr("Find &Usages"), QKeySequence(tr("Alt+F7")), this,
                                            inEditMode([this] { intelligence_->findUsages(); }));
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
    navigate->addSeparator();
    // F7, as IntelliJ's next difference.
    navigate->addAction(tr("Next Conflict"), QKeySequence(tr("F7")), this,
                        [this] { goToConflict(MergeConflicts::Direction::Next); });
    navigate->addAction(tr("Previous Conflict"), QKeySequence(tr("Shift+F7")), this,
                        [this] { goToConflict(MergeConflicts::Direction::Previous); });
}

QAction *MainWindow::createHelpMenu()
{
    QMenu *help = titleBar_->menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Check &Prerequisites…"), this, &MainWindow::showPrerequisites);
    help->addSeparator();
    QAction *about = help->addAction(tr("&About Kodamiko"), this, &MainWindow::showAbout);
    about->setMenuRole(QAction::AboutRole);
    return about;
}

void MainWindow::showAbout()
{
    if (!aboutPopup_)
        aboutPopup_ = new AboutPopup(theme_, this);
    aboutPopup_->popup();
}

void MainWindow::applyKeymap(const Keymap::Overrides &overrides)
{
    const QList<Keymap::Command> commands = Keymap::commands(titleBar_->menuBar());
    Keymap::apply(commands, overrides);
    EditorWidget::setReservedKeys(Keymap::shortcutsInUse(commands));
}

int MainWindow::addRecentProjects(QMenu *menu)
{
    int listed = 0;
    for (const ProjectConfig &project : ProjectStore::all()) {
        if (project.path == folder_ || !QFileInfo(project.path).isDir() || listed == recentProjectCount)
            continue;
        const QString folder = project.path;
        QAction *action = menu->addAction(QFileInfo(folder).fileName(), this,
                                          [this, folder] { askToOpenProject(folder, AfterOpening::Nothing); });
        action->setToolTip(QDir::toNativeSeparators(folder));
        action->setIcon(FileIcons::folder(theme_.isDark)); // the project chip's
        ++listed;
    }
    menu->setToolTipsVisible(true);
    return listed;
}

// The Dock icon's menu (macOS): the other projects, listed when it opens.
void MainWindow::createDockMenu()
{
    auto *dock = new QMenu(this);
    connect(dock, &QMenu::aboutToShow, this, [this, dock] {
        dock->clear();
        if (addRecentProjects(dock) > 0)
            dock->addSeparator();
        dock->addAction(tr("Open Folder…"), this, &MainWindow::chooseFolder);
    });
    DesktopIntegration::setDockMenu(dock);
}

void MainWindow::updateProjectMenu()
{
    projectMenu_->clear();
    projectMenu_->addAction(tr("Project Settings…"), this, &MainWindow::showProjectSettings);
    for (const Recipe &recipe : std::as_const(recipes_)) {
        if (!recipe.setupCommands.isEmpty())
            projectMenu_->addAction(tr("Run %1 Setup").arg(recipe.title), this,
                                    [this, recipe] { runRecipeSetup(recipe); });
    }
    addRecipePrompts(projectMenu_);
    projectMenu_->addSeparator();
    if (addRecentProjects(projectMenu_) > 0)
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
