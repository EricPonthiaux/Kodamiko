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
    QDateTime date;        // author date
    QStringList parentIds; // full identifiers, the first parent first
    // Branches and tags pointing to this commit ("main", "origin/main",
    // "v1.0"), with HEAD first if it points to it.
    QStringList refs;
    // In a file's history: its path in this commit, relative to the open
    // folder; an older one before a rename. Empty in other logs.
    QString path;

    bool operator==(const CommitSummary &) const = default;
};

// Which commits a log walks: those of HEAD, or of every local and remote
// branch (HEAD included, even detached).
enum class LogScope {
    Head,
    AllBranches
};

// Commits of HEAD (or of every branch), newest first, a commit before its
// parents. If the open folder is a subfolder of the
// repository, only the commits that modify it are kept; with `path` (relative
// to the open folder), only those that modify that file or folder. A file is
// followed across its renames within the open folder, like "git log
// --follow".
struct CommitLog {
    RepositoryInfo info;
    QList<CommitSummary> commits;
    bool truncated = false; // more than `limit` commits: the rest is not read

    bool operator==(const CommitLog &) const = default;

    // Returns nullopt outside a git repository. Throws GitError on failure.
    static std::optional<CommitLog> read(const QString &folder,
                                         int limit = 2000,
                                         const QString &path = {},
                                         LogScope scope = LogScope::Head);
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
    static CommitDetails read(const QString &folder,
                              const QString &commitId,
                              Whitespace whitespace = Whitespace::Compare);
};
