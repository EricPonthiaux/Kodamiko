// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "FilePatch.h"
#include "RepositoryStatus.h"

#include <QStringList>

// Operations that modify the repository. They go through the git command: the
// repository's hooks, commit signing and the user's configuration apply as in
// a terminal. Paths are relative to the repository root.
//
// Synchronous: run them off the UI thread (see GitCommands).
// They throw GitError with git's message.
namespace GitOperations {

// Stages the on-disk state of the files (created, modified or deleted).
void stage(const RepositoryInfo &repository, const QStringList &paths);
// Unstages the files; their contents on disk do not change.
void unstage(const RepositoryInfo &repository, const QStringList &paths);
// Applies a patch built by FilePatch::partial to the index.
void applyToIndex(const RepositoryInfo &repository, const QByteArray &patch, PatchDirection direction);

// Restores the files on disk to their state in the index; untracked files
// are deleted. A snapshot is taken first (see Snapshots).
void discard(const RepositoryInfo &repository, const QStringList &paths);
// Restores the files, on disk and in the index, to their state in the last
// commit: staged changes go too, and the files added since are removed from
// the index and deleted. A snapshot is taken first (see Snapshots).
void rollback(const RepositoryInfo &repository, const QStringList &paths);
// Removes from the files on disk the lines of a patch built by
// FilePatch::partial(…, PatchDirection::Reverse) from an "index → disk"
// diff. `paths`: affected files, for the snapshot taken first.
void discardPatch(const RepositoryInfo &repository, const QStringList &paths, const QByteArray &patch);

// Commits the index (or replaces the last commit if `amend`). Returns the
// abbreviated identifier of the created commit. Throws a GitError of kind
// NoIdentity when git does not know who commits (see setIdentity).
QString commit(const RepositoryInfo &repository, const QString &message, bool amend);

// Who commits: git's user.name and user.email.
struct Identity {
    QString name;
    QString email;
};

// Where setIdentity writes: the user's configuration (~/.gitconfig), for
// every repository, or the repository's own.
enum class ConfigScope {
    Global,
    Repository,
};

// The identity git's configuration gives in `repository`; a part that is
// not set is empty.
Identity identity(const RepositoryInfo &repository);
// Writes user.name and user.email in `scope`.
void setIdentity(const RepositoryInfo &repository, const Identity &identity, ConfigScope scope);

// Creates an empty repository in `folder` ("git init"): its branch is the
// one the user's init.defaultBranch names.
void init(const QString &folder);

// Deletes a file from the working directory, along with the folders above
// it that become empty.
void removeFile(const QString &workDir, const QString &path);

} // namespace GitOperations
