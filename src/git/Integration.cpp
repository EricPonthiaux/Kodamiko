// SPDX-License-Identifier: GPL-3.0-only

#include "Integration.h"

#include "FileChange.h"
#include "GitOperations.h"
#include "GitProcess.h"
#include "Snapshots.h"

#include <QObject>
#include <QSet>

namespace Integration {
namespace {

// The git command of an operation in progress, as RepositoryInfo names it.
QString commandOf(const QString &operation)
{
    if (operation == QObject::tr("merge"))
        return QStringLiteral("merge");
    if (operation == QObject::tr("rebase"))
        return QStringLiteral("rebase");
    if (operation == QObject::tr("cherry-pick"))
        return QStringLiteral("cherry-pick");
    if (operation == QObject::tr("revert"))
        return QStringLiteral("revert");
    return {};
}

QString commandOrThrow(const RepositoryInfo &repository)
{
    const QString command = commandOf(repository.operation);
    if (command.isEmpty()) {
        throw GitError(
            repository.operation.isEmpty()
                ? QObject::tr("No merge or rebase is in progress")
                : QObject::tr("A %1 cannot be finished from the editor: use a terminal").arg(repository.operation));
    }
    return command;
}

QString integrate(const RepositoryInfo &repository, const QStringList &arguments, const QString &reason)
{
    Snapshots::createIfChanged(repository, reason);
    try {
        return QString::fromUtf8(Git::run(repository.workDir, arguments, {}, Git::englishMessages())).trimmed();
    } catch (const GitError &error) {
        if (error.message().contains(QLatin1String("would be overwritten")))
            throw GitError(error.message(), GitError::Kind::LocalChanges);
        throw;
    }
}

// The index stage of each side of a conflict (1 is the common ancestor).
int stageOf(ConflictSide side)
{
    switch (side) {
    case ConflictSide::Current:
        return 2;
    case ConflictSide::Incoming:
        return 3;
    }
    return 2;
}

// The conflicted files among `paths` that have a version at `stage`: the
// others were deleted on that side.
QSet<QString> pathsWithStage(const RepositoryInfo &repository, const QStringList &paths, int stage)
{
    // "<mode> <object> <stage>\t<path>" for each version of a conflicted file.
    const QStringList entries =
        Git::splitNul(Git::run(repository.workDir, QStringList{QStringLiteral("ls-files"), QStringLiteral("-u"),
                                                               QStringLiteral("-z"), QStringLiteral("--")} +
                                                       paths));
    QSet<QString> result;
    for (const QString &entry : entries) {
        const qsizetype tab = entry.indexOf(QLatin1Char('\t'));
        const bool isAtStage = tab > 0 && entry.at(tab - 1).digitValue() == stage;
        if (isAtStage)
            result.insert(entry.mid(tab + 1));
    }
    return result;
}

} // namespace

QString merge(const RepositoryInfo &repository, const QString &branch)
{
    return integrate(repository, {QStringLiteral("merge"), QStringLiteral("--no-edit"), branch},
                     QObject::tr("Before merging %1").arg(branch));
}

QString rebase(const RepositoryInfo &repository, const QString &onto)
{
    return integrate(repository, {QStringLiteral("rebase"), onto}, QObject::tr("Before rebasing onto %1").arg(onto));
}

QString cherryPick(const RepositoryInfo &repository, const QString &id)
{
    return integrate(repository, {QStringLiteral("cherry-pick"), id}, QObject::tr("Before cherry-picking %1").arg(id));
}

QString revert(const RepositoryInfo &repository, const QString &id)
{
    return integrate(repository, {QStringLiteral("revert"), QStringLiteral("--no-edit"), id},
                     QObject::tr("Before reverting %1").arg(id));
}

void continueOperation(const RepositoryInfo &repository)
{
    // The message stays the one git prepared (GIT_EDITOR does nothing).
    Git::run(repository.workDir, {commandOrThrow(repository), QStringLiteral("--continue")});
}

void abortOperation(const RepositoryInfo &repository)
{
    Git::run(repository.workDir, {commandOrThrow(repository), QStringLiteral("--abort")});
}

void skipCommit(const RepositoryInfo &repository)
{
    if (!canSkip(repository.operation))
        throw GitError(QObject::tr("Only a rebase, cherry-pick or revert can skip a commit"));
    Git::run(repository.workDir, {commandOrThrow(repository), QStringLiteral("--skip")});
}

void acceptVersion(const RepositoryInfo &repository, const QStringList &paths, ConflictSide side)
{
    if (paths.isEmpty())
        return;
    const QString reason = side == ConflictSide::Current ? QObject::tr("Before accepting the current version")
                                                         : QObject::tr("Before accepting the incoming version");
    Snapshots::create(repository, reason, paths);
    const QSet<QString> kept = pathsWithStage(repository, paths, stageOf(side));
    QStringList checkedOut;
    QStringList deleted;
    for (const QString &path : paths)
        (kept.contains(path) ? checkedOut : deleted).append(path);
    if (!checkedOut.isEmpty()) {
        const QString option = side == ConflictSide::Current ? QStringLiteral("--ours") : QStringLiteral("--theirs");
        Git::run(repository.workDir,
                 QStringList{QStringLiteral("checkout"), option, QStringLiteral("--")} + checkedOut);
        GitOperations::stage(repository, checkedOut);
    }
    if (!deleted.isEmpty())
        Git::run(repository.workDir,
                 QStringList{QStringLiteral("rm"), QStringLiteral("-q"), QStringLiteral("-f"), QStringLiteral("--")} +
                     deleted);
}

bool canContinue(const QString &operation)
{
    return !commandOf(operation).isEmpty();
}

bool canSkip(const QString &operation)
{
    const QString command = commandOf(operation);
    return !command.isEmpty() && command != QLatin1String("merge");
}

} // namespace Integration
