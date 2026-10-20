// SPDX-License-Identifier: GPL-3.0-only

#include "RepositoryWatcher.h"

#include <QDir>
#include <QFileInfo>

RepositoryWatcher::RepositoryWatcher(QObject *parent)
    : QObject(parent)
{
    debounce_.setSingleShot(true);
    debounce_.setInterval(300);
    connect(&debounce_, &QTimer::timeout, this, &RepositoryWatcher::changed);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, &debounce_, qOverload<>(&QTimer::start));
}

void RepositoryWatcher::setGitDir(const QString &gitDir)
{
    if (gitDir == gitDir_)
        return;
    gitDir_ = gitDir;
    if (!watcher_.directories().isEmpty())
        watcher_.removePaths(watcher_.directories());
    if (gitDir.isEmpty())
        return;

    // HEAD and index are in the git folder; branches in refs/heads, or in
    // the common folder for a secondary worktree. refs holds the stash;
    // refs/remotes and its folders, the remote branches a fetch updates.
    const auto refsDirectories = [](const QString &folder) {
        const QDir refs(QDir(folder).filePath(QStringLiteral("refs")));
        QStringList directories{refs.path(), refs.filePath(QStringLiteral("heads")),
                                refs.filePath(QStringLiteral("remotes"))};
        const QDir remotes(refs.filePath(QStringLiteral("remotes")));
        for (const QString &remote : remotes.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            directories.append(remotes.filePath(remote));
        return directories;
    };
    QStringList directories{gitDir};
    directories << refsDirectories(gitDir);
    QFile commonDirFile(QDir(gitDir).filePath(QStringLiteral("commondir")));
    if (commonDirFile.open(QIODevice::ReadOnly)) {
        const QString commonDir = QDir(gitDir).absoluteFilePath(QString::fromUtf8(commonDirFile.readAll()).trimmed());
        directories << commonDir << refsDirectories(commonDir);
    }
    for (const QString &directory : std::as_const(directories)) {
        if (QFileInfo(directory).isDir())
            watcher_.addPath(directory);
    }
}
