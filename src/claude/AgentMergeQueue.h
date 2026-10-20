// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/Worktrees.h"

#include <QList>
#include <QObject>
#include <QString>

class GitCommands;
class QProcess;

// Merges the agents' commits into the checkout's current branch, one agent at
// a time: replays them on the branch in the agent's worktree, runs the
// project's check command there, then moves the branch forward to them.
// Commits that conflict with the branch or fail the check are not merged:
// the agent is sent the problem to fix.
class AgentMergeQueue : public QObject {
    Q_OBJECT

public:
    enum class AfterMerge {
        KeepWorktree,
        RemoveWorktree, // the agent's session is closed
    };

    // Runs its git work on `git`'s queue (not owned).
    explicit AgentMergeQueue(GitCommands *git, QObject *parent = nullptr);

    // The checkout whose current branch receives the agents' work.
    void setFolder(const QString &folder) { folder_ = folder; }

    // A shell command run in the worktree once the commits are replayed;
    // its failure stops the merge. Empty: no check.
    void setCheckCommand(const QString &command) { checkCommand_ = command; }

    // Queues the worktree's commits; nothing if they are queued already.
    void enqueue(const Worktree &worktree, AfterMerge afterMerge = AfterMerge::KeepWorktree);

signals:
    // A step of the agent's merge started ("Checking agent-1's commits").
    void progress(const QString &message);
    // `branch` now ends with the worktree's commits, up to `head`.
    void merged(const Worktree &worktree, const QString &branch, const QString &head, AfterMerge afterMerge);
    // The agent has to act: its commits up to `head` conflict with the
    // branch, or fail the check. `report` tells it what happened and what to
    // do.
    void sentBack(const Worktree &worktree, const QString &report, const QString &head);
    // The merge stopped for a reason the user handles: their changes in the
    // way, no current branch, git failing.
    void failed(const Worktree &worktree, const QString &message);

private:
    struct Request {
        Worktree worktree;
        AfterMerge afterMerge = AfterMerge::KeepWorktree;
        int attempts = 0;     // replays, redone when the branch moved meanwhile
        QString replayedHead; // the worktree's last commit after the replay
    };

    // The request being merged: the first of the queue.
    Request &current() { return queue_.first(); }

    void finishCurrent();
    // Starts the current request's merge: replays its commits.
    void rebase();
    void onRebased(const RebaseResult &result, const QString &branch, const QString &head);
    void runCheck(const QString &branch);
    void onChecked(int exitCode, const QString &branch);
    void fastForward(const QString &branch);
    void fail(const QString &message);
    void sendBack(const QString &report);

    GitCommands *git_;
    QString folder_;
    QString checkCommand_;
    // The first is being merged, the others wait.
    QList<Request> queue_;
    QProcess *check_ = nullptr; // the running check, a child
};
