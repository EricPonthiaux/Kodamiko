// SPDX-License-Identifier: GPL-3.0-only

// MainWindow: git services, the Git menu, branches, stashes, snapshots and the
// branch shown in the status bar.

#include "MainWindow.h"
#include "changes/BranchesPopup.h"
#include "changes/ChangesView.h"
#include "changes/GitActions.h"
#include "changes/NewBranchPopup.h"
#include "changes/SnapshotsPopup.h"
#include "changes/StashesPopup.h"
#include "claude/ClaudeConfigView.h"
#include "claude/EditorContext.h"
#include "claude/mcp/DuetTools.h"
#include "editor/EditorTabs.h"
#include "git/Askpass.h"
#include "git/Branches.h"
#include "git/FileStatuses.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"
#include "git/Integration.h"
#include "git/RepositoryWatcher.h"
#include "history/HistoryView.h"
#include "navigation/SymbolIndex.h"
#include "ui/TitleBar.h"
#include "workspace/ChoicePopup.h"
#include "workspace/EntryPopups.h"
#include "workspace/FileTreeView.h"

#include <QDir>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>

namespace {

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

void MainWindow::createGitServices()
{
    // Git operations: result in the status bar, errors (hook output
    // included) in a dialog box.
    gitCommands_ = new GitCommands(this);
    agentMerges_ = new AgentMergeQueue(gitCommands_, this);
    connect(agentMerges_, &AgentMergeQueue::progress, this, &MainWindow::showStatus);
    connect(agentMerges_, &AgentMergeQueue::merged, this, &MainWindow::onAgentMerged);
    connect(agentMerges_, &AgentMergeQueue::sentBack, this, &MainWindow::onAgentSentBack);
    connect(agentMerges_, &AgentMergeQueue::waiting, this, &MainWindow::onAgentMergeWaiting);
    connect(agentMerges_, &AgentMergeQueue::failed, this, &MainWindow::onAgentMergeFailed);
    connect(gitCommands_, &GitCommands::succeeded, this, [this](const QString &, const QString &message) {
        if (!message.isEmpty())
            showStatus(message);
        updateRepositoryInfo();
    });
    connect(gitCommands_, &GitCommands::failed, this, [this](const QString &title, const QString &error) {
        showNotice({.title = tr("%1: failed").arg(title), .location = {}, .message = error, .details = {}}, this);
        updateRepositoryInfo();
        stopClosingAgents(tr("%1 failed").arg(title));
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
        QTimer::singleShot(0, this, &MainWindow::closeWindowOnceAgentsClosed);
    });
    // git and ssh ask for credentials through duet-askpass.
    askpass_ = new AskpassServer([this](const QString &prompt) { return askCredentials(prompt); }, this);
    if (askpass_->listen())
        gitCommands_->setRemoteEnvironment(askpass_->environment());
    gitActions_ = new GitActions(gitCommands_, this, [this] { return folder_; }, this);
    connect(gitActions_, &GitActions::changed, this, &MainWindow::onRepositoryChanged);
    connect(gitActions_, &GitActions::statusMessage, this, &MainWindow::showStatus);
    repositoryWatcher_ = new RepositoryWatcher(this);
    connect(repositoryWatcher_, &RepositoryWatcher::changed, this, &MainWindow::onRepositoryChanged);
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

void MainWindow::runGitOnTree(const QString &title, const QString &path, bool rollback)
{
    std::optional<RepositoryInfo> info;
    try {
        info = RepositoryInfo::read(folder_);
    } catch (const GitError &error) {
        showNotice({.title = tr("%1: failed").arg(title), .location = {}, .message = error.message(), .details = {}},
                   this);
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
            GitOperations::rollback(info, paths);
            return count == 1 ? QObject::tr("1 file rolled back") : QObject::tr("%1 files rolled back").arg(count);
        }
        GitOperations::stage(info, paths);
        return QObject::tr("Added to the index");
    });
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
        showStatus(QString::fromStdString(error.what()));
        return;
    }
    if (!repository) {
        showStatus(tr("The folder is not in a git repository"));
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

void MainWindow::askClaudeForCommitMessage(const QString &request)
{
    if (!exposeTools_->isChecked()) {
        showStatus(tr("Claude hands the commit message back through Duet's tools: turn on Claude ▸ Expose Duet's "
                      "Tools to Claude, then restart the session"));
        return;
    }
    typeInClaudePrompt(request, PromptEntry::Submitted);
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
        bool trusted = false;
        auto *popup =
            new ChoicePopup({.title = tr("Unknown Host"), .location = {}, .message = prompt.trimmed(), .details = {}},
                            {{tr("Trust and Connect"), [&trusted] { trusted = true; }}}, this);
        popup->focusCancel();
        popup->exec();
        return trusted ? QStringLiteral("yes") : QStringLiteral("no");
    }
    std::optional<QString> answer;
    auto *popup = new NamePopup(
        tr("Git Credentials"), prompt.trimmed(),
        [&answer](const QString &typed) {
            answer = typed;
            return QString();
        },
        this);
    popup->field()->setPlaceholderText({});
    // A user name is shown as typed; passwords and passphrases are not.
    const bool secret = !prompt.startsWith(QLatin1String("Username"), Qt::CaseInsensitive);
    popup->field()->setEchoMode(secret ? QLineEdit::Password : QLineEdit::Normal);
    popup->exec();
    return answer;
}

void MainWindow::showSnapshots()
{
    snapshotsPopup_->popup(folder_);
}
