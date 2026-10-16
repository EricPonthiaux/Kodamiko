#pragma once

#include "RepositoryStatus.h"

#include <QDateTime>
#include <QStringList>

// Copy of working-directory files, taken before an operation that overwrites
// them (discarding changes, a Claude prompt…). It is a commit stored under
// refs/enlumine/snapshots/, outside any branch: it does not appear in the
// history, but git keeps it.
struct Snapshot {
    QString ref;
    QString commitId;
    QDateTime date;
    QString reason;
    QStringList paths; // repository paths, as they were on disk
    // The commit holds the whole working directory (ignored files excepted),
    // not only `paths`: restoring it also undoes changes made afterwards to
    // other files.
    bool wholeWorkingTree = false;
};

namespace Snapshots {

// The most recent ones are kept, the others deleted.
constexpr int maximumCount = 100;

// Records the on-disk state of `paths` (repository paths), untracked files
// included, without touching the index or the files. Returns the created
// reference. Throws GitError.
QString create(const RepositoryInfo &repository, const QString &reason, const QStringList &paths);

// Records the whole working directory (ignored files excepted), even when it
// is clean; `paths` lists the files that differed from the last commit.
// Takes nothing and returns an empty string when the working directory is
// identical to the most recent snapshot. Throws GitError.
QString createOfWorkingTree(const RepositoryInfo &repository, const QString &reason);

// Snapshots of the repository containing `folder`, newest first.
// Throws GitError.
QList<Snapshot> list(const QString &folder);

// Restores the snapshot's files on disk as it recorded them (a file missing
// from the snapshot is deleted), after taking a snapshot of their current
// state. For a snapshot of the whole working directory, every file that
// differs from it is restored. The index is not modified. Returns the number
// of files written or deleted. Throws GitError.
int restore(const RepositoryInfo &repository, const Snapshot &snapshot);

} // namespace Snapshots
