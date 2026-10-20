// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "CommitLog.h"

#include <QByteArray>
#include <QRegularExpression>

struct git_commit;
struct git_patch;
struct git_repository;

// A ContentSearch made ready to run on each commit of a log: the regular
// expression compiled once.
class ContentMatcher {
public:
    // Throws GitError if the pattern of a ChangedLine search is not a valid
    // regular expression.
    explicit ContentMatcher(const ContentSearch &search);

    // Whether the changes of `commit` against its first parent (against an
    // empty tree for the first commit), limited to `pathspec` (a repository
    // path; empty: everywhere), involve the pattern. False for a merge, as in
    // git log. Throws GitError on failure.
    bool matches(git_repository *repository, const git_commit *commit, const QString &pathspec) const;

private:
    bool patchMatches(git_patch *patch) const;

    ContentMatch match_;
    QByteArray text_; // Occurrences: the pattern in UTF-8
    QRegularExpression expression_;
};
