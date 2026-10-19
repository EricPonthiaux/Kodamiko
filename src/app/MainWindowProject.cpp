// MainWindow: opening and closing a project, its session, its settings and
// the tools it needs; terminals.

#include "MainWindow.h"
#include "changes/ChangesView.h"
#include "claude/ClaudeConfigView.h"
#include "claude/EditorContext.h"
#include "claude/mcp/DuetTools.h"
#include "completion/CompletionController.h"
#include "completion/CompletionHistory.h"
#include "completion/CompletionService.h"
#include "diagnostics/ProblemStore.h"
#include "diagnostics/ProblemsView.h"
#include "editor/EditorTabs.h"
#include "git/FileStatuses.h"
#include "history/HistoryView.h"
#include "lsp/LanguageServers.h"
#include "navigation/GoToPopup.h"
#include "projects/ProjectStore.h"
#include "run/Runner.h"
#include "search/FindInFilesPopup.h"
#include "settings/PreferencesPopup.h"
#include "setup/PrerequisitesDialog.h"
#include "terminal/TerminalWidget.h"
#include "toolchains/InstallPlan.h"
#include "ui/NoticeBar.h"
#include "ui/UiIcons.h"
#include "welcome/ProjectSettingsPopup.h"
#include "welcome/ProjectSetupForm.h"
#include "workspace/FileTreeView.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
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
