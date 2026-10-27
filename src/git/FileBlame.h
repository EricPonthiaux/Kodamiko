// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "CommitLog.h"

#include <QList>
#include <QStringList>

// Which commit last changed each line of a file, as "git blame" says it, in
// the file's version at HEAD.
struct FileBlame {
    QStringList lines; // the file at HEAD, without line ends
    // The commits that wrote the lines, in the order their first line comes;
    // without refs, parents or path.
    QList<CommitSummary> commits;
    // For each line, the index of its commit in `commits`.
    QList<int> lineCommits;

    bool operator==(const FileBlame &) const = default;

    // `relativePath`: a file of the open folder `folder`. Throws GitError
    // outside a repository and for a file HEAD does not have.
    static FileBlame read(const QString &folder, const QString &relativePath);
};
