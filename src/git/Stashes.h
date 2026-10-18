#pragma once

#include "RepositoryStatus.h"

#include <QDateTime>
#include <QList>

// Changes put aside with "git stash", to bring back later on this branch or
// another one. Synchronous: run the changes off the UI thread (see
// GitCommands). Each throws GitError.
struct Stash {
    int index = 0;   // stash@{index}: 0 is the most recent
    QString message; // "On main: Fix the login" or "WIP on main: 1234abc Subject"
    QDateTime date;
};

namespace Stashes {

// The stashes, the most recent first.
QList<Stash> list(const QString &workDir);

// Puts aside the changes of the index and the working directory (untracked
// files too with `includeUntracked`), which then match the last commit.
// Fails when there is nothing to put aside.
void push(const RepositoryInfo &repository, const QString &message, bool includeUntracked);

// Brings back the stash's changes; pop() also drops it, unless its changes
// conflict with the files: the conflicts are left to resolve, and the stash
// kept. Fails with GitError::Kind::LocalChanges when local changes would be
// overwritten.
void apply(const RepositoryInfo &repository, int index);
void pop(const RepositoryInfo &repository, int index);
void drop(const RepositoryInfo &repository, int index);

// The stash's changes as a unified diff, untracked files included.
QByteArray patch(const QString &workDir, int index);

} // namespace Stashes
