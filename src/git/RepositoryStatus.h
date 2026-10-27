// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "FileChange.h"

#include <QByteArray>
#include <QList>

#include <optional>

// Git repository containing the open folder, and the state of its HEAD.
struct RepositoryInfo {
    QString workDir; // root of the working directory, without a trailing "/"
    QString gitDir;  // git folder of the working directory (".git")
    // Path of the open folder within the repository ("src/"), empty at the root.
    QString prefix;

    QString branch; // empty if HEAD is detached
    QString headId; // abbreviated identifier of the last commit, empty without commits
    QString headMessage;
    bool detached = false;
    bool unborn = false; // repository without commits
    // Operation in progress ("merge", "rebase"…), empty otherwise.
    QString operation;
    // Remote branch the current branch tracks ("origin/main"), empty if
    // none; and the number of commits on either side only, as of the last
    // fetch.
    QString upstream;
    int ahead = 0;
    int behind = 0;

    // Repository path of a path relative to the open folder.
    QString repoPath(const QString &relativePath) const { return prefix + relativePath; }

    bool operator==(const RepositoryInfo &) const = default;

    // Returns nullopt if `folder` belongs to no git repository (or to a
    // repository without a working directory). Throws GitError on failure.
    static std::optional<RepositoryInfo> read(const QString &folder);
};

// How diffs treat whitespace: compared as any other text, or ignored, as
// "git diff -w" does. An ignoring patch no longer matches the files: it
// shows the changes but cannot be applied.
enum class Whitespace {
    Compare,
    Ignore
};

// Changed file and its diff.
struct FileDiff {
    FileChange change;
    // Raw unified diff (paths relative to the repository root): the file's
    // bytes are kept as is, so that it can be reapplied. Empty when
    // `isTooLarge`.
    QByteArray patch;
    // A file too large to be compared line by line (over 1 MiB on either
    // side): only listed.
    bool isTooLarge = false;
    // The new version's bytes, for the unchanged lines a view can show
    // around the blocks: only for a text file modified or renamed, where the
    // diff holds a part of it.
    QByteArray newText;
    // For a conflicted file, which has no patch: what its version on disk
    // holds.
    ConflictContent conflict = ConflictContent::None;

    bool operator==(const FileDiff &) const = default;
};

// Pending changes, split as in "git status". Only the files of the open
// folder are compared, and their paths are relative to that folder. The
// repository is only read.
struct RepositoryStatus {
    RepositoryInfo info;
    QList<FileDiff> staged;   // last commit → index
    QList<FileDiff> unstaged; // index → disk, untracked files included

    bool operator==(const RepositoryStatus &) const = default;

    // Returns nullopt outside a git repository. Throws GitError on failure.
    static std::optional<RepositoryStatus> read(const QString &folder, Whitespace whitespace = Whitespace::Compare);
};
