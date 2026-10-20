// SPDX-License-Identifier: GPL-3.0-only

#include "GitOperations.h"

#include "GitProcess.h"
#include "Snapshots.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace {

QStringList withPaths(QStringList arguments, const QStringList &paths)
{
    arguments << QStringLiteral("--");
    arguments += paths;
    return arguments;
}

QSet<QString> trackedPaths(const RepositoryInfo &repository, const QStringList &paths)
{
    const QStringList tracked = Git::splitNul(
        Git::run(repository.workDir, withPaths({QStringLiteral("ls-files"), QStringLiteral("-z")}, paths)));
    return {tracked.begin(), tracked.end()};
}

QString discardReason(const QStringList &paths)
{
    return paths.size() == 1
               ? QObject::tr("Before discarding changes to %1").arg(paths.first())
               : QObject::tr("Before discarding changes to %n file(s)", nullptr, static_cast<int>(paths.size()));
}

} // namespace

namespace GitOperations {

void stage(const RepositoryInfo &repository, const QStringList &paths)
{
    if (!paths.isEmpty())
        Git::run(repository.workDir, withPaths({QStringLiteral("add"), QStringLiteral("-A")}, paths));
}

void unstage(const RepositoryInfo &repository, const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    // Without commits, there is no version to put back in the index: the
    // files are simply removed from it.
    if (repository.unborn)
        Git::run(repository.workDir, withPaths({QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"),
                                                QStringLiteral("-q"), QStringLiteral("--ignore-unmatch")},
                                               paths));
    else
        Git::run(repository.workDir, withPaths({QStringLiteral("restore"), QStringLiteral("--staged")}, paths));
}

void applyToIndex(const RepositoryInfo &repository, const QByteArray &patch, PatchDirection direction)
{
    if (patch.isEmpty())
        return;
    QStringList arguments{QStringLiteral("apply"), QStringLiteral("--cached"), QStringLiteral("--recount")};
    if (direction == PatchDirection::Reverse)
        arguments << QStringLiteral("-R");
    arguments << QStringLiteral("-");
    Git::run(repository.workDir, arguments, patch);
}

void discard(const RepositoryInfo &repository, const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    Snapshots::create(repository, discardReason(paths), paths);

    const QSet<QString> tracked = trackedPaths(repository, paths);
    QStringList toRestore;
    for (const QString &path : paths) {
        if (tracked.contains(path))
            toRestore.append(path);
        else
            removeFile(repository.workDir, path);
    }
    if (!toRestore.isEmpty())
        Git::run(repository.workDir, withPaths({QStringLiteral("restore"), QStringLiteral("--worktree")}, toRestore));
}

void rollback(const RepositoryInfo &repository, const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    Snapshots::create(repository, discardReason(paths), paths);
    if (repository.unborn) {
        // No commit yet: every file is new.
        Git::run(repository.workDir,
                 withPaths({QStringLiteral("rm"), QStringLiteral("--quiet"), QStringLiteral("--cached")}, paths));
        for (const QString &path : paths)
            removeFile(repository.workDir, path);
        return;
    }
    Git::run(repository.workDir, withPaths({QStringLiteral("restore"), QStringLiteral("--source=HEAD"),
                                            QStringLiteral("--staged"), QStringLiteral("--worktree")},
                                           paths));
}

void discardPatch(const RepositoryInfo &repository, const QStringList &paths, const QByteArray &patch)
{
    if (patch.isEmpty())
        return;
    Snapshots::create(repository, discardReason(paths), paths);
    Git::run(repository.workDir,
             {QStringLiteral("apply"), QStringLiteral("--recount"), QStringLiteral("-R"), QStringLiteral("-")}, patch);
}

QString commit(const RepositoryInfo &repository, const QString &message, bool amend)
{
    QStringList arguments{QStringLiteral("commit"), QStringLiteral("--quiet"), QStringLiteral("--cleanup=whitespace"),
                          QStringLiteral("-F"), QStringLiteral("-")};
    if (amend)
        arguments << QStringLiteral("--amend");
    Git::run(repository.workDir, arguments, message.toUtf8());
    return QString::fromUtf8(Git::run(repository.workDir,
                                      {QStringLiteral("rev-parse"), QStringLiteral("--short"), QStringLiteral("HEAD")}))
        .trimmed();
}

void init(const QString &folder)
{
    Git::run(folder, {QStringLiteral("init"), QStringLiteral("--quiet")});
}

void removeFile(const QString &workDir, const QString &path)
{
    const QString absolute = QDir(workDir).filePath(path);
    if (QFileInfo::exists(absolute) && !QFile::remove(absolute))
        throw GitError(QObject::tr("Cannot delete %1").arg(path));
    const QString root = QDir::cleanPath(workDir);
    QDir parent = QFileInfo(absolute).dir();
    while (QDir::cleanPath(parent.absolutePath()).size() > root.size() && parent.isEmpty()) {
        const QString name = parent.dirName();
        if (!parent.cdUp() || !parent.rmdir(name))
            break;
    }
}

} // namespace GitOperations
