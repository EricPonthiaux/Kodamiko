// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "GitProcess.h"
#include "RepositoryStatus.h"

#include <QDateTime>
#include <QStringList>

// A local branch, or a remote one as of the last fetch.
struct BranchInfo {
    QString name; // "main", "feature/x"; "origin/main" for a remote branch
    bool remote = false;
    bool current = false;
    // Remote branch a local branch tracks ("origin/main"), empty if none.
    QString upstream;
    bool upstreamGone = false; // deleted on the remote
    int ahead = 0;
    int behind = 0;
    QDateTime date;  // of its last commit
    QString subject; // first line of its last commit's message

    bool operator==(const BranchInfo &) const = default;
};

// Branches: listing, naming, creating, switching, renaming and deleting
// them. The changes are synchronous: run them off the UI thread (see
// GitCommands). Each throws GitError.
namespace Branches {

// Why `name` cannot name a branch (git check-ref-format's rules), or an
// empty string when it can.
QString nameError(const QString &name);

// What the user typed, made into a branch name: spaces become "-", the
// characters git refuses are dropped ("Fix the login bug" → "Fix-the-login-bug").
QString sanitizedName(const QString &typed);

// Names of the repository's local branches ("main", "feature/x").
// Throws GitError.
QStringList local(const QString &workDir);

// Local branches, then remote ones, each by name.
QList<BranchInfo> list(const QString &workDir);

// Creates the branch `name` at HEAD; with `switchTo`, also makes it the
// current branch, keeping the changes of the working directory and the
// index. In a repository without commits, `switchTo` renames the branch to
// come. Synchronous: run it off the UI thread (see GitCommands). Throws
// GitError.
void create(const RepositoryInfo &repository, const QString &name, bool switchTo);

// Makes `name` the current branch, after taking a snapshot of the working
// directory if it has changes; the changes that do not conflict follow.
// For a remote branch ("origin/x"), switches to the local branch "x",
// created to track it if needed. Fails with GitError::Kind::LocalChanges
// when local changes would be overwritten.
void switchTo(const RepositoryInfo &repository, const QString &name, bool remote);

// Same, putting the changes aside first and bringing them back on `name`.
// When they conflict with it, the switch is done and the conflicts are left
// to resolve: throws GitError, the changes still in the stash.
void stashAndSwitch(const RepositoryInfo &repository, const QString &name, bool remote);

// Checks out the commit `id` without a branch (detached HEAD).
void detach(const RepositoryInfo &repository, const QString &id);

void rename(const RepositoryInfo &repository, const QString &name, const QString &newName);

// Deletes the local branch `name`. Unless `force`, fails with
// GitError::Kind::Unmerged when its commits are in no other branch.
void remove(const RepositoryInfo &repository, const QString &name, bool force);

// Deletes the branch `name` ("origin/x") on its remote.
void removeRemote(const RepositoryInfo &repository, const QString &name, const Git::Feedback &feedback);

// Makes the local branch `name` track the remote branch `upstream` ("origin/x").
void setUpstream(const RepositoryInfo &repository, const QString &name, const QString &upstream);

// The remote of a remote branch ("origin" for "origin/feature/x"), and the
// branch's name there ("feature/x").
QString remoteOf(const QString &remoteBranch);
QString nameOnRemote(const QString &remoteBranch);

} // namespace Branches
