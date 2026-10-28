// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "RepositoryStatus.h"

#include <QString>
#include <QStringList>

// Bringing another branch's commits into the current branch (merge,
// rebase), and finishing or giving up the operation in progress when it
// stops on conflicts. Synchronous: run them off the UI thread (see
// GitCommands). Each throws GitError.
namespace Integration {

// Merges `branch` into the current branch, with git's default message,
// after taking a snapshot of the working directory if it has changes.
// Returns git's summary. On conflicts, throws GitError: the merge stays in
// progress (RepositoryInfo::operation) until it is continued or aborted.
QString merge(const RepositoryInfo &repository, const QString &branch);

// Replays the current branch's own commits on top of `onto`; same
// snapshot and conflicts as merge().
QString rebase(const RepositoryInfo &repository, const QString &onto);

// Applies the changes of the commit `id` to the current branch as a new
// commit; same snapshot and conflicts as merge().
QString cherryPick(const RepositoryInfo &repository, const QString &id);

// Commits the reverse of the commit `id`, with git's default message; same
// snapshot and conflicts as merge().
QString revert(const RepositoryInfo &repository, const QString &id);

// For the operation in progress (`repository.operation`): commit the
// resolution and go on, give up and restore the state from before, or (a
// rebase only) leave out the commit that stopped it.
void continueOperation(const RepositoryInfo &repository);
void abortOperation(const RepositoryInfo &repository);
void skipCommit(const RepositoryInfo &repository);

// A side of the conflicts of the operation in progress (see
// ConflictMarkers): the branch checked out, or what is brought into it.
enum class ConflictSide {
    Current,
    Incoming,
};

// Resolves the conflicted files `paths` (repository paths) by taking their
// version on `side` whole, and marks them resolved in the index. A file that
// side deleted is deleted. A snapshot of the files is taken first.
void acceptVersion(const RepositoryInfo &repository, const QStringList &paths, ConflictSide side);

// Whether `operation` can be continued, aborted and skipped from the editor.
bool canContinue(const QString &operation);
bool canSkip(const QString &operation);

} // namespace Integration
