// SPDX-License-Identifier: GPL-3.0-only

#include "AgentMergeQueue.h"

#include "git/FileChange.h"
#include "git/GitCommands.h"
#include "git/RepositoryStatus.h"

#include <QProcess>

#include <algorithm>
#include <memory>

namespace {

// Replays redone when the branch moves between a replay and the merge.
constexpr int maxAttempts = 3;
// Lines of a failing check's output sent to the agent.
constexpr int reportedOutputLines = 60;

struct RebaseOutcome {
    RebaseResult result;
    QString branch; // the checkout's current branch, replayed on
    QString head;   // the worktree's last commit afterwards
};

QString lastLines(const QString &output, int count)
{
    const QStringList lines = output.trimmed().split(QLatin1Char('\n'));
    return lines.mid(std::max<qsizetype>(0, lines.size() - count)).join(QLatin1Char('\n'));
}

// The checkout's current branch, which receives the work.
QString currentBranch(const QString &folder)
{
    const std::optional<RepositoryInfo> repository = RepositoryInfo::read(folder);
    if (!repository)
        throw GitError(QObject::tr("The folder is not in a git repository"));
    if (repository->detached || repository->branch.isEmpty())
        throw GitError(QObject::tr("The checkout has no current branch to merge into."));
    return repository->branch;
}

} // namespace

AgentMergeQueue::AgentMergeQueue(GitCommands *git, QObject *parent)
    : QObject(parent)
    , git_(git)
{
}

void AgentMergeQueue::enqueue(const Worktree &worktree, AfterMerge afterMerge)
{
    const auto queued = std::ranges::find_if(
        queue_, [&worktree](const Request &request) { return request.worktree.path == worktree.path; });
    if (queued != queue_.end()) {
        // A closing session's work goes when merged, whoever queued it first.
        if (afterMerge == AfterMerge::RemoveWorktree)
            queued->afterMerge = afterMerge;
        return;
    }
    queue_.append({.worktree = worktree, .afterMerge = afterMerge});
    if (queue_.size() == 1)
        rebase();
}

void AgentMergeQueue::finishCurrent()
{
    queue_.removeFirst();
    if (!queue_.isEmpty())
        rebase();
}

void AgentMergeQueue::rebase()
{
    ++current().attempts;
    emit progress(tr("Replaying %1's commits").arg(current().worktree.name));
    // Filled by the job, off the UI thread, then read on it: shared by the two.
    const auto outcome = std::make_shared<RebaseOutcome>();
    git_->run(
        tr("Replay %1's commits").arg(current().worktree.name),
        [folder = folder_, worktree = current().worktree, outcome] {
            outcome->branch = currentBranch(folder);
            outcome->result = Worktrees::rebase(worktree, outcome->branch);
            outcome->head = Worktrees::head(worktree);
            return QString();
        },
        [this, outcome] { onRebased(outcome->result, outcome->branch, outcome->head); },
        [this](const GitError &error) {
            fail(error.message());
            return true;
        });
}

void AgentMergeQueue::onRebased(const RebaseResult &result, const QString &branch, const QString &head)
{
    current().replayedHead = head;
    if (!result.pendingOperation.isEmpty()) {
        // The agent is busy with it: it ends another turn once it is done.
        emit progress(tr("%1 is in the middle of a %2: its commits are not merged yet")
                          .arg(current().worktree.name, result.pendingOperation));
        finishCurrent();
        return;
    }
    if (!result.conflicts.isEmpty()) {
        sendBack(tr("Duet could not merge your commits into %1: replayed on it, they conflict in %2. Run `git rebase "
                    "%1` in your worktree, resolve the conflicts and finish the rebase, then end your turn: Duet "
                    "merges again.")
                     .arg(branch, result.conflicts.join(QStringLiteral(", "))));
        return;
    }
    if (checkCommand_.trimmed().isEmpty()) {
        fastForward(branch);
        return;
    }
    runCheck(branch);
}

void AgentMergeQueue::runCheck(const QString &branch)
{
    emit progress(tr("Checking %1's commits: %2").arg(current().worktree.name, checkCommand_));
    check_ = new QProcess(this);
    check_->setWorkingDirectory(current().worktree.path);
    check_->setProcessChannelMode(QProcess::MergedChannels);
    connect(check_, &QProcess::finished, this, [this, branch](int exitCode, QProcess::ExitStatus status) {
        onChecked(status == QProcess::NormalExit ? exitCode : -1, branch);
    });
    connect(check_, &QProcess::errorOccurred, this, [this, branch](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            onChecked(-1, branch);
    });
#ifdef Q_OS_WIN
    check_->start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), checkCommand_});
#else
    check_->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), checkCommand_});
#endif
}

void AgentMergeQueue::onChecked(int exitCode, const QString &branch)
{
    const QString output = QString::fromLocal8Bit(check_->readAll());
    check_->deleteLater();
    check_ = nullptr;
    if (exitCode == 0) {
        fastForward(branch);
        return;
    }
    sendBack(tr("Duet did not merge your commits into %1: the project's check failed (`%2`, exit code %3). Fix it, "
                "commit, then end your turn: Duet merges again.\n\nLast lines of its output:\n%4")
                 .arg(branch, checkCommand_, QString::number(exitCode), lastLines(output, reportedOutputLines)));
}

void AgentMergeQueue::fastForward(const QString &branch)
{
    emit progress(tr("Merging %1's commits into %2").arg(current().worktree.name, branch));
    // Filled by the job, off the UI thread, then read on it: shared by the two.
    const auto head = std::make_shared<QString>();
    git_->run(
        tr("Merge %1 into %2").arg(current().worktree.name, branch),
        [folder = folder_, worktree = current().worktree, head] {
            const std::optional<RepositoryInfo> repository = RepositoryInfo::read(folder);
            if (!repository)
                throw GitError(QObject::tr("The folder is not in a git repository"));
            Worktrees::fastForward(*repository, worktree.branch);
            *head = Worktrees::head(worktree);
            return QObject::tr("%1's commits merged into %2").arg(worktree.name, repository->branch);
        },
        [this, branch, head] {
            const Request request = current();
            emit merged(request.worktree, branch, *head, request.afterMerge);
            finishCurrent();
        },
        [this](const GitError &error) {
            const bool branchMoved = error.kind() == GitError::Kind::Diverged && current().attempts < maxAttempts;
            if (branchMoved)
                rebase(); // new commits on the branch since the replay: replay again
            else
                fail(error.message());
            return true;
        });
}

void AgentMergeQueue::fail(const QString &message)
{
    emit failed(current().worktree, message);
    finishCurrent();
}

void AgentMergeQueue::sendBack(const QString &report)
{
    emit sentBack(current().worktree, report, current().replayedHead);
    finishCurrent();
}
