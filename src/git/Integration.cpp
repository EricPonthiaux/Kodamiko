#include "Integration.h"

#include "FileChange.h"
#include "GitProcess.h"
#include "Snapshots.h"

#include <QObject>

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
