#include "FileWatcher.h"

#include <QFileInfo>

namespace {
constexpr int debounceMs = 150;
}

FileWatcher::FileWatcher(QObject *parent)
    : QObject(parent)
{
    debounce_.setSingleShot(true);
    debounce_.setInterval(debounceMs);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &FileWatcher::onFileChanged);
    connect(&debounce_, &QTimer::timeout, this, &FileWatcher::emitPendingChanges);
}

void FileWatcher::watch(const QString &path)
{
    watchedPaths_.insert(path);
    watcher_.addPath(path);
}

void FileWatcher::unwatch(const QString &path)
{
    watchedPaths_.remove(path);
    pendingPaths_.remove(path);
    watcher_.removePath(path);
}

void FileWatcher::onFileChanged(const QString &path)
{
    if (!watchedPaths_.contains(path))
        return;
    pendingPaths_.insert(path);
    debounce_.start();
}

void FileWatcher::emitPendingChanges()
{
    const QSet<QString> paths = std::exchange(pendingPaths_, {});
    for (const QString &path : paths) {
        // An "atomic" save replaces the file with a new one: the system then
        // stops watching the path, which must be added again.
        if (QFileInfo::exists(path) && !watcher_.files().contains(path))
            watcher_.addPath(path);
        emit fileChanged(path);
    }
}
