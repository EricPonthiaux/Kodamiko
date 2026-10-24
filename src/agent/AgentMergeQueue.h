// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ProjectCheck.h"
#include "git/FileStatuses.h"
#include "git/Worktrees.h"

#include <QList>
#include <QObject>
#include <QString>

class GitCommands;
class GitError;

// Merges the agents' commits into the checkout's current branch, one agent at
// a time: replays them on the branch in the agent's worktree, runs the
// project's check command there, then moves the branch forward to them.
// Commits that conflict with the branch or fail the check are not merged:
// the agent is sent the problem to fix. Commits the user's uncommitted
// changes keep out wait, and go in once those files are clean again.
class AgentMergeQueue : public QObject {
    Q_OBJECT

public:
    enum class AfterMerge {
        KeepWorktree,
        RemoveWorktree, // the agent's session is closed
    };

    // Runs its git work on `git`'s queue (not owned).
    explicit AgentMergeQueue(GitCommands *git, QObject *parent = nullptr);

    // The checkout whose current branch receives the agents' work. The work
    // waiting for another checkout is forgotten.
    void setFolder(const QString &folder);

    // A shell command run in the worktree once the commits are replayed;
    // its failure stops the merge. Empty: no check.
    void setCheckCommand(const QString &command) { checkCommand_ = command; }

    // Queues the worktree's commits; nothing if they are queued already.
    // Commits waiting for the user's files are queued again at once.
    void enqueue(const Worktree &worktree, AfterMerge afterMerge = AfterMerge::KeepWorktree);
    // Follows the checkout's file states (not owned; it reads the folder
    // given to setFolder()): when they change, the work waiting for files
    // now clean is queued again, and the work of deleted worktrees dropped.
    void watch(const FileStatusReader *statuses);
    // The worktrees whose work waits for the user's files.
    QList<Worktree> waitingWorktrees() const;

    // Whether work is queued or being merged.
    bool isMerging() const { return !queue_.isEmpty(); }

signals:
    // A step of the agent's merge started ("Checking agent-1's commits").
    void progress(const QString &message);
    // `branch` now ends with the worktree's commits, up to `head`.
    void merged(const Worktree &worktree, const QString &branch, const QString &head, AfterMerge afterMerge);
    // The agent has to act: its commits up to `head` conflict with the
    // branch, or fail the check. `report` tells it what happened and what to
    // do.
    void sentBack(const Worktree &worktree, const QString &report, const QString &head);
    // The user's uncommitted changes to `files` (relative to the folder)
    // would be overwritten: the work waits until they are committed,
    // discarded or stashed (see watch()).
    void waiting(const Worktree &worktree, const QStringList &files);
    // The merge stopped for a reason the user handles: no current branch,
    // git failing.
    void failed(const Worktree &worktree, const QString &message);

private:
    struct Request {
        Worktree worktree;
        AfterMerge afterMerge = AfterMerge::KeepWorktree;
        int attempts = 0;          // replays, redone when the branch moved meanwhile
        QString replayedHead;      // the worktree's last commit after the replay
        QStringList blockingFiles; // the user's changed files in the way, absolute
    };

    // The request being merged: the first of the queue.
    Request &current() { return queue_.first(); }

    void finishCurrent();
    // Starts the current request's merge: replays its commits.
    void rebase();
    void onRebased(const RebaseResult &result, const QString &branch, const QString &head);
    void runCheck(const QString &branch);
    void onChecked(const ProjectCheck::Result &result, const QString &branch);
    void fastForward(const QString &branch);
    void onFastForwardFailed(const GitError &error, const QStringList &blockingFiles);
    void fail(const QString &message);
    void retryWaiting(const FileStatusMap &statuses);
    // Drops the worktree's waiting work.
    void forget(const Worktree &worktree);
    // Sets the current request aside until `files` are clean.
    void wait(const QStringList &files);
    void sendBack(const QString &report);

    GitCommands *git_;
    QString folder_;
    QString checkCommand_;
    // The first is being merged, the others wait.
    QList<Request> queue_;
    // Kept out by the user's changes, apart from the queue.
    QList<Request> waiting_;
    ProjectCheck *check_ = nullptr; // the running check, a child
};
