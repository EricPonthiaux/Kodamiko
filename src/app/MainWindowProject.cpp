// SPDX-License-Identifier: GPL-3.0-only

// MainWindow: opening and closing a project, its session, its settings and
// the tools it needs; terminals.

#include "MainWindow.h"
#include "agent/EditorContext.h"
#include "agent/mcp/DuetTools.h"
#include "agent/vendor/claude/ClaudeConfigView.h"
#include "changes/ChangesView.h"
#include "completion/CompletionController.h"
#include "completion/CompletionHistory.h"
#include "completion/CompletionService.h"
#include "diagnostics/ProblemStore.h"
#include "diagnostics/ProblemsView.h"
#include "editor/EditorTabs.h"
#include "git/FileStatuses.h"
#include "history/HistoryView.h"
#include "index/OccurrenceIndex.h"
#include "lsp/LanguageServers.h"
#include "navigation/GoToPopup.h"
#include "navigation/HierarchyView.h"
#include "navigation/UsagesView.h"
#include "projects/ProjectStore.h"
#include "recipes/RecipeLibrary.h"
#include "run/Runner.h"
#include "search/FindInFilesPopup.h"
#include "settings/PreferencesPopup.h"
#include "setup/PrerequisitesPopup.h"
#include "terminal/TerminalWidget.h"
#include "toolchains/InstallPlan.h"
#include "ui/DetachablePage.h"
#include "ui/NoticeBar.h"
#include "ui/UiIcons.h"
#include "welcome/NewProjectPopup.h"
#include "welcome/ProjectSettingsPopup.h"
#include "welcome/ProjectSetupForm.h"
#include "workspace/ChoicePopup.h"
#include "workspace/FileTreeView.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>

#include <algorithm>

void MainWindow::openFolder(const QString &folder)
{
    if (!folder_.isEmpty()) {
        saveSession();
        if (!editorTabs_->closeAll())
            return; // unsaved changes: the user cancelled
    }
    const QString previousFolder = folder_;
    folder_ = QDir(folder).absolutePath();
    setAgentProvider(projectAgentProvider());
    fileTree_->setRootPath(folder_);
    editorTabs_->setProjectFolder(folder_);
    claudeConfigView_->setProjectFolder(folder_);
    claudeTouched_.clear();
    updateClaudeMarks();
    symbolIndex_->setFolder(folder_);
    occurrenceIndex_->setFolder(folder_);
    completion_->history()->setFile(CompletionHistory::fileFor(folder_)); // the project's choices
    project_ = ProjectStore::touch(folder_);
    if (!project_.configured) {
        // Opened without the welcome window: set up from its files.
        project_ = ProjectSetupForm::detectedProject(project_, *toolchains_);
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
    usagesView_->setProjectFolder(folder_);
    hierarchyView_->clear();
    runner_->setProject(project_, toolchains_.get());
    loadProjectRecipes();
    agentMerges_->setFolder(folder_);
    agentMerges_->setCheckCommand(project_.agentCheckCommand);
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
    terminalWindow_->setWindowTitle(tr("Terminals — %1").arg(QFileInfo(folder_).fileName()));
    updateProjectMenu();
    if (project_.needsRecipeSetup) {
        for (const Recipe &recipe : std::as_const(recipes_))
            runRecipeSetup(recipe);
    }
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
    // A new project: its detected setup, to check.
    askToOpenProject(folder, imported ? AfterOpening::ShowSettings : AfterOpening::Nothing);
}

void MainWindow::askToOpenProject(const QString &folder, AfterOpening after)
{
    if (folder_.isEmpty() || QDir(folder).absolutePath() == folder_) {
        openProjectHere(folder, after);
        return;
    }
    const QString name = QFileInfo(folder).fileName();
    // The agents' worktrees belong to the current project: it stays open
    // until they are closed and merged (Close Project).
    const bool hasAgentWork = !agentSessions_.isEmpty() || agentMerges_->isMerging();
    QList<ChoicePopup::Choice> choices{{tr("New Window"), [this, folder] { openProjectInNewWindow(folder); }}};
    if (!hasAgentWork)
        choices.append({tr("This Window"), [this, folder, after] { openProjectHere(folder, after); }});
    const QString message =
        hasAgentWork
            ? tr("Open %1 in a new Duet window?").arg(name)
            : tr("Open %1 in a new Duet window, or here in place of %2?").arg(name, QFileInfo(folder_).fileName());
    auto *popup = new ChoicePopup({.title = tr("Open Project"),
                                   .location = QDir::toNativeSeparators(folder),
                                   .message = message,
                                   .details = projectSwitchCost(hasAgentWork)},
                                  choices, this);
    popup->popup();
}

QString MainWindow::projectSwitchCost(bool hasAgentWork) const
{
    const QString agent = agentProvider_->name();
    if (hasAgentWork)
        return tr("%1 sessions are running in this project's worktrees: close them first to open another "
                  "project in this window.")
            .arg(agent);
    if (!claudeTerminal_ || !claudeTerminal_->isRunning())
        return {};
    if (agentActivity_.state() == AgentActivity::State::Working)
        return tr("%1 is working: opening the project here stops it and ends this window's %1 session.").arg(agent);
    return tr("Opening the project here ends this window's %1 session.").arg(agent);
}

void MainWindow::openProjectHere(const QString &folder, AfterOpening after)
{
    openFolder(folder);
    const bool isOpen = folder_ == QDir(folder).absolutePath();
    if (isOpen && after == AfterOpening::ShowSettings)
        showProjectSettings();
}

void MainWindow::openProjectInNewWindow(const QString &folder)
{
    // In an AppImage, the executable lives in a mount that goes away with
    // this process: the new one starts from the image itself.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString program = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
    if (QProcess::startDetached(program, {QDir(folder).absolutePath()})) {
        showStatus(tr("Opening %1 in a new window").arg(QFileInfo(folder).fileName()));
        return;
    }
    showNotice({.title = tr("Open Project"),
                .location = QDir::toNativeSeparators(folder),
                .message = tr("Duet could not start a new window: %1 did not run.").arg(program),
                .details = {}},
               this);
}

void MainWindow::showNewProject()
{
    if (!newProjectPopup_) {
        newProjectPopup_ = new NewProjectPopup(theme_, this);
        connect(newProjectPopup_, &NewProjectPopup::created, this, &MainWindow::openNewProject);
    }
    newProjectPopup_->popup();
}

void MainWindow::openNewProject(const QString &folder)
{
    // Its setup, detected from the template's files, to check.
    askToOpenProject(folder, AfterOpening::ShowSettings);
}

void MainWindow::showProjectSettings()
{
    projectSettings()->popup(project_);
}

ProjectSettingsPopup *MainWindow::projectSettings()
{
    // Built on first use: its toolchain panel takes tens of milliseconds,
    // too long for every project opening.
    if (!projectSettings_) {
        projectSettings_ = new ProjectSettingsPopup(toolchains_.get(), theme_, this);
        connect(projectSettings_, &ProjectSettingsPopup::applied, this, &MainWindow::applyProject);
    }
    return projectSettings_;
}

void MainWindow::applyProject(const ProjectConfig &project)
{
    if (QDir(project.path).absolutePath() != folder_)
        return; // another project's downloads, finished after a switch
    const bool recipesChanged = project.recipes() != project_.recipes();
    project_ = project;
    languageServers_->setProject(project_, toolchains_.get());
    runner_->setProject(project_, toolchains_.get());
    // Its runtimes may have been downloaded just now.
    loadProjectRecipes();
    if (recipesChanged) {
        updateProjectMenu();
        showStatus(tr("Recipes changed: the terminals and the %1 session started from now on use them")
                       .arg(agentProvider_->name()));
    }
    agentMerges_->setCheckCommand(project_.agentCheckCommand);
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
    // The agents' sessions closing first, it closes later.
    if (!close() && !isClosingAgents_)
        closingProject_ = false;
}

void MainWindow::updateToolsNotice()
{
    const QList<ToolRequirement> required = RecipeLibrary::requirementsOf(project_);
    const QList<InstallStep> steps = InstallPlan::plan(project_, *toolchains_, required);
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
    showTerminals();
    terminal->start(spec);
    terminal->setFocus();
    return terminal;
}

void MainWindow::showPrerequisites()
{
    if (!prerequisitesPopup_) {
        prerequisitesPopup_ = new PrerequisitesPopup(theme_, this);
        connect(prerequisitesPopup_, &PrerequisitesPopup::installRequested, this,
                [this](const QString &title, const ProcessSpec &spec) {
                    const TerminalWidget *terminal = addTerminal(title, spec);
                    connect(terminal, &TerminalWidget::finished, prerequisitesPopup_, &PrerequisitesPopup::popup);
                });
        connect(prerequisitesPopup_, &PrerequisitesPopup::checked, this, [this] {
            // Freshly installed programs: use them right away.
            updateRepositoryInfo();
            if (mode_ == Mode::Diff)
                changesView_->refresh();
            const bool claudeFound = std::any_of(prerequisitesPopup_->prerequisites().begin(),
                                                 prerequisitesPopup_->prerequisites().end(), [](const Prerequisite &p) {
                                                     return p.id == Prerequisite::Id::ClaudeCode &&
                                                            p.status != Prerequisite::Status::Missing;
                                                 });
            if (claudeFound && (!claudeTerminal_ || !claudeTerminal_->isRunning()))
                startClaudeSession();
        });
    }
    prerequisitesPopup_->popup();
}
