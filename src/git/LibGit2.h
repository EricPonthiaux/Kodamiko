// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "RepositoryStatus.h"

#include <git2.h>

#include <memory>

// Shared helpers for reading the repository with libgit2. Reserved for the
// git module's .cpp files: includes <git2.h> only where it is needed.
namespace libgit2 {

struct Free {
    void operator()(git_repository *p) const { git_repository_free(p); }

    void operator()(git_object *p) const { git_object_free(p); }

    void operator()(git_tree *p) const { git_tree_free(p); }

    void operator()(git_commit *p) const { git_commit_free(p); }

    void operator()(git_reference *p) const { git_reference_free(p); }

    void operator()(git_reference_iterator *p) const { git_reference_iterator_free(p); }

    void operator()(git_index *p) const { git_index_free(p); }

    void operator()(git_diff *p) const { git_diff_free(p); }

    void operator()(git_patch *p) const { git_patch_free(p); }

    void operator()(git_revwalk *p) const { git_revwalk_free(p); }

    void operator()(git_tree_entry *p) const { git_tree_entry_free(p); }

    void operator()(git_blob *p) const { git_blob_free(p); }
};

template <typename T>
using Ptr = std::unique_ptr<T, Free>;

// Throws GitError if `result` reports an error.
inline void check(int result, const char *action)
{
    if (result >= 0)
        return;
    const git_error *error = git_error_last();
    throw GitError(QStringLiteral("%1: %2").arg(QString::fromUtf8(action), error ? QString::fromUtf8(error->message)
                                                                                 : QStringLiteral("unknown error")));
}

// Keeps libgit2 initialized for the duration of a read.
struct Session {
    Session() { git_libgit2_init(); }

    ~Session() { git_libgit2_shutdown(); }

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
};

// Opens the repository containing `folder`; returns nullptr if there is none
// (or if it has no working directory).
Ptr<git_repository> openRepository(const QString &folder);

FileChange fileChangeOf(const git_diff_delta *delta, const QString &prefix);
// One FileDiff per file in `diff`, paths stripped of `prefix`.
QList<FileDiff> fileDiffs(git_diff *diff, const QString &prefix);

} // namespace libgit2
