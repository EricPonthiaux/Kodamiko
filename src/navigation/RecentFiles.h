#pragma once

#include <QStringList>

// Recently opened files (Ctrl+E) and files recently edited by Claude, most
// recent first. Absolute paths.
class RecentFiles {
public:
    static constexpr qsizetype maxSize = 50;

    // The file was opened or became the current tab.
    void touch(const QString &path);
    // Claude wrote these files (Write, Edit… tools).
    void markEditedByClaude(const QStringList &paths);
    // The file or folder `path` was deleted, or renamed `newPath`.
    void remove(const QString &path);
    void rename(const QString &oldPath, const QString &newPath);

    QStringList opened() const { return opened_; }
    QStringList editedByClaude() const { return editedByClaude_; }
    bool isEditedByClaude(const QString &path) const { return editedByClaude_.contains(path); }

    void restore(const QStringList &opened, const QStringList &editedByClaude);
    void clear();

private:
    QStringList opened_;
    QStringList editedByClaude_;
};
