#pragma once

#include <QString>

#include <stdexcept>

enum class ChangeKind { Added, Modified, Deleted, Renamed, Conflicted };

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
    explicit GitError(const QString &message)
        : std::runtime_error(message.toStdString())
    {
    }
};
