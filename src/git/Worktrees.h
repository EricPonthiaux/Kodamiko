// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "RepositoryStatus.h"

#include <QString>
#include <QStringList>

// A worktree Duet created for a Claude session besides the main one: a
// second working directory of the same repository, on a branch of its own,
// so that the session's edits never touch the user's checkout.
struct Worktree {
    QString name;       // "agent-1": also the session's name
    QString path;       // "<repository>/.claude/worktrees/agent-1"
    QString branch;     // "duet/agent-1"
    QString baseCommit; // full identifier of the commit it started from
};

// What replaying a worktree's commits on a branch gave.
struct RebaseResult {
    // The operation the worktree was in the middle of ("rebase", "merge"…):
    // nothing was replayed, so as not to undo it. Empty if none.
    QString pendingOperation;
    // Files both sides changed; the rebase was undone. Empty on success.
    QStringList conflicts;

    bool succeeded() const { return pendingOperation.isEmpty() && conflicts.isEmpty(); }
};

// The worktrees of Claude sessions: creating, inspecting, merging and
// removing them.
// Synchronous: run them off the UI thread (see GitCommands). Each throws
// GitError.
namespace Worktrees {

// Folder holding them: "<repository>/.claude/worktrees", as `claude
// --worktree` does.
QString folder(const RepositoryInfo &repository);

// A name no worktree of the folder uses yet: "agent-1", "agent-2"…
QString unusedName(const RepositoryInfo &repository);

// Creates the worktree `name` on a new branch "duet/<name>" at HEAD. The
// folder is excluded locally (.git/info/exclude, never the versioned
// .gitignore), so the project tree, the search and git status ignore the
// copies. Uncommitted changes of the checkout do not follow.
Worktree add(const RepositoryInfo &repository, const QString &name);

// Whether the worktree holds work: uncommitted changes, or commits since it
// started.
bool hasWork(const Worktree &worktree);

// Whether the worktree has commits since its base: work to merge.
bool hasCommitsToMerge(const Worktree &worktree);

// Commits every change of the worktree, untracked files included, with
// `message`; nothing when it has none.
void commitChanges(const Worktree &worktree, const QString &message);

// Replays the worktree's commits on `onto` (a branch of the repository).
// Uncommitted changes are put aside meanwhile and brought back. Nothing
// happens while the worktree is in the middle of an operation (the agent's
// own rebase…): see RebaseResult::pendingOperation.
RebaseResult rebase(const Worktree &worktree, const QString &onto);

// Moves the checkout's current branch forward to `branch`, as "git merge
// --ff-only" does. Fails with GitError::Kind::LocalChanges when the
// checkout's changes would be overwritten, GitError::Kind::Diverged when
// the current branch has commits `branch` lacks, and GitError::Kind::General
// without a current branch (detached HEAD).
void fastForward(const RepositoryInfo &repository, const QString &branch);

// The checkout's files that stopped fastForward() with
// GitError::Kind::LocalChanges, from its message: absolute paths, changed or
// untracked files git would have overwritten.
QStringList blockingFiles(const RepositoryInfo &repository, const QString &mergeError);

// The identifier of the worktree's last commit.
QString head(const Worktree &worktree);

// Deletes the worktree, its changes and its branch.
void remove(const RepositoryInfo &repository, const Worktree &worktree);

} // namespace Worktrees
