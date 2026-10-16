#pragma once

#include "RepositoryStatus.h"

#include <QDateTime>
#include <QList>
#include <QStringList>

#include <optional>

// Commit as shown in the history list.
struct CommitSummary {
    QString id;      // full identifier
    QString shortId; // first 7 characters
    QString summary; // first line of the message
    QString author;
    QString email;
    QDateTime date; // author date
    int parentCount = 0;
    // Branches and tags pointing to this commit ("main", "origin/main",
    // "v1.0"), with HEAD first if it points to it.
    QStringList refs;
};

// Commits of HEAD, newest first. If the open folder is a subfolder of the
// repository, only the commits that modify it are kept; with `path` (relative
// to the open folder), only those that modify that file or folder.
struct CommitLog {
    RepositoryInfo info;
    QList<CommitSummary> commits;
    bool truncated = false; // more than `limit` commits: the rest is not read

    // Returns nullopt outside a git repository. Throws GitError on failure.
    static std::optional<CommitLog> read(const QString &folder, int limit = 2000, const QString &path = {});
};

// Contents of a commit: full message and changes relative to its first
// parent (to an empty tree for the first commit), limited to the open
// folder. Paths relative to that folder.
struct CommitDetails {
    CommitSummary summary;
    QString message;
    QString committer; // empty if it is also the author
    QDateTime commitDate;
    QStringList parentIds; // abbreviated
    QList<FileDiff> files;

    // Throws GitError on failure (no repository, unknown commit…).
    static CommitDetails read(const QString &folder, const QString &commitId);
};
