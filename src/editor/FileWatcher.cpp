// SPDX-License-Identifier: GPL-3.0-only

#include "FileWatcher.h"

#include <QFileInfo>

#include <algorithm>

namespace {

constexpr int debounceMs = 150;

QString folderOf(const QString &path)
{
    return QFileInfo(path).absolutePath();
}

} // namespace

FileWatcher::FileWatcher(QObject *parent)
    : QObject(parent)
{
    debounce_.setSingleShot(true);
    debounce_.setInterval(debounceMs);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &FileWatcher::onFileChanged);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, &FileWatcher::onFolderChanged);
    connect(&debounce_, &QTimer::timeout, this, &FileWatcher::emitPendingChanges);
}

void FileWatcher::watch(const QString &path)
{
    watchedPaths_.insert(path);
    watcher_.addPath(path);
    watcher_.addPath(folderOf(path));
}

void FileWatcher::unwatch(const QString &path)
{
    watchedPaths_.remove(path);
    pendingPaths_.remove(path);
    watcher_.removePath(path);
    const QString folder = folderOf(path);
    const auto isInFolder = [&folder](const QString &watched) { return folderOf(watched) == folder; };
    if (std::ranges::none_of(watchedPaths_, isInFolder))
        watcher_.removePath(folder);
}

void FileWatcher::onFileChanged(const QString &path)
{
    if (!watchedPaths_.contains(path))
        return;
    pendingPaths_.insert(path);
    debounce_.start();
}

// The system stops watching a deleted file: a file of `folder` that exists
// again is watched again, and reported as changed.
void FileWatcher::onFolderChanged(const QString &folder)
{
    const QStringList watchedFiles = watcher_.files();
    for (const QString &path : std::as_const(watchedPaths_)) {
        const bool isBack = folderOf(path) == folder && !watchedFiles.contains(path) && QFileInfo::exists(path);
        if (!isBack)
            continue;
        watcher_.addPath(path);
        onFileChanged(path);
    }
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
