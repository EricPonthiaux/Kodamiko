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
    // the common folder for a secondary worktree.
    QStringList directories{gitDir, QDir(gitDir).filePath(QStringLiteral("refs/heads"))};
    QFile commonDirFile(QDir(gitDir).filePath(QStringLiteral("commondir")));
    if (commonDirFile.open(QIODevice::ReadOnly)) {
        const QString commonDir = QDir(gitDir).absoluteFilePath(QString::fromUtf8(commonDirFile.readAll()).trimmed());
        directories << commonDir << QDir(commonDir).filePath(QStringLiteral("refs/heads"));
    }
    for (const QString &directory : std::as_const(directories)) {
        if (QFileInfo(directory).isDir())
            watcher_.addPath(directory);
    }
}
