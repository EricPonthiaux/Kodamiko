// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

#include <stdexcept>

enum class ChangeKind {
    Added,
    Modified,
    Deleted,
    Renamed,
    Conflicted
};

// File changed between two states of the repository (last commit, index, disk).
// Paths relative to the project root, "/" separators.
struct FileChange {
    ChangeKind kind;
    QString path;
    QString oldPath; // set only for ChangeKind::Renamed

    bool operator==(const FileChange &) const = default;
};

class GitError : public std::runtime_error {
public:
    // The failures the interface offers a way out of; General for the others.
    enum class Kind {
        General,
        Cancelled,    // stopped by the user
        LocalChanges, // local changes would be overwritten (switch, merge…)
        Rejected,     // push refused: the remote branch has commits we lack
        Diverged,     // pull cannot fast-forward: both sides have new commits
        Unmerged,     // branch deletion refused: its commits are merged nowhere
        InWorktree,   // branch deletion refused: a worktree has it checked out
    };

    explicit GitError(const QString &message, Kind kind = Kind::General)
        : std::runtime_error(message.toStdString())
        , kind_(kind)
    {
    }

    Kind kind() const { return kind_; }

    QString message() const { return QString::fromStdString(what()); }

private:
    Kind kind_;
};
