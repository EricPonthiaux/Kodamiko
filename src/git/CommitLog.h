// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "RepositoryStatus.h"

#include <QDateTime>
#include <QList>
#include <QStringList>

#include <optional>
#include <stop_token>

// Commit as shown in the history list.
struct CommitSummary {
    QString id;      // full identifier
    QString shortId; // first 7 characters
    QString summary; // first line of the message
    QString message; // the whole message, summary included
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

// How a content search reads the changes of a commit.
enum class ContentMatch {
    // The text occurs a different number of times before and after the
    // commit, as "git log -S" finds it: the commits that add or remove it.
    Occurrences,
    // A changed line (added or removed) matches the regular expression, as
    // "git log -G" finds it.
    ChangedLine
};

// Commits whose changes involve `pattern`, case sensitive as in git.
struct ContentSearch {
    ContentMatch match = ContentMatch::Occurrences;
    QString pattern;

    bool operator==(const ContentSearch &) const = default;
};

// Which commits CommitLog::read keeps.
struct LogOptions {
    int limit = 2000;
    // A file or folder relative to the open folder: only the commits that
    // modify it. Empty: every commit of the open folder.
    QString path;
    LogScope scope = LogScope::Head;
    // Only the commits whose changes (against their first parent) involve a
    // text; merges are left out, as by git log.
    std::optional<ContentSearch> contents;
    // Requested: the walk stops, the log is left incomplete.
    std::stop_token stop;
};

// Commits of HEAD (or of every branch), newest first, a commit before its
// parents. If the open folder is a subfolder of the
// repository, only the commits that modify it are kept; with a path, only
// those that modify that file or folder. A file is followed across its
// renames within the open folder, like "git log --follow".
struct CommitLog {
    RepositoryInfo info;
    QList<CommitSummary> commits;
    bool truncated = false; // more than `limit` commits: the rest is not read

    bool operator==(const CommitLog &) const = default;

    // Returns nullopt outside a git repository. Throws GitError on failure,
    // and for a content search with an invalid regular expression.
    static std::optional<CommitLog> read(const QString &folder, const LogOptions &options = {});
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
