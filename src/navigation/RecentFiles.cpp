#include "RecentFiles.h"

#include <QDir>

namespace {

void moveToFront(QStringList &paths, const QString &path)
{
    paths.removeAll(path);
    paths.prepend(path);
    if (paths.size() > RecentFiles::maxSize)
        paths.resize(RecentFiles::maxSize);
}

bool isUnder(const QString &path, const QString &folder)
{
    return path == folder || path.startsWith(folder + QLatin1Char('/'));
}

void renameIn(QStringList &paths, const QString &oldPath, const QString &newPath)
{
    for (QString &path : paths) {
        if (isUnder(path, oldPath))
            path = newPath + path.mid(oldPath.size());
    }
}

} // namespace

void RecentFiles::touch(const QString &path)
{
    if (!path.isEmpty())
        moveToFront(opened_, QDir::cleanPath(path));
}

void RecentFiles::markEditedByClaude(const QStringList &paths)
{
    for (const QString &path : paths)
        moveToFront(editedByClaude_, QDir::cleanPath(path));
}

void RecentFiles::remove(const QString &path)
{
    const QString removed = QDir::cleanPath(path);
    const auto under = [&removed](const QString &recent) { return isUnder(recent, removed); };
    opened_.removeIf(under);
    editedByClaude_.removeIf(under);
}

void RecentFiles::rename(const QString &oldPath, const QString &newPath)
{
    renameIn(opened_, QDir::cleanPath(oldPath), QDir::cleanPath(newPath));
    renameIn(editedByClaude_, QDir::cleanPath(oldPath), QDir::cleanPath(newPath));
}

void RecentFiles::restore(const QStringList &opened, const QStringList &editedByClaude)
{
    opened_ = opened.mid(0, maxSize);
    editedByClaude_ = editedByClaude.mid(0, maxSize);
}

void RecentFiles::clear()
{
    opened_.clear();
    editedByClaude_.clear();
}
