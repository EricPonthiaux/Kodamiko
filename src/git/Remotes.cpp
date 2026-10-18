#include "Remotes.h"

#include "FileChange.h"

#include <QObject>

#include <optional>

namespace Remotes {
namespace {

Git::RunOptions options(const Git::Feedback &feedback)
{
    return Git::RunOptions{{}, Git::englishMessages(), feedback};
}

// Value of a git setting, nothing when it is not set.
std::optional<QString> configValue(const QString &workDir, const QString &key)
{
    try {
        return QString::fromUtf8(Git::run(workDir, {QStringLiteral("config"), QStringLiteral("--get"), key})).trimmed();
    } catch (const GitError &) {
        return std::nullopt; // exit code 1: not set
    }
}

void requireBranch(const RepositoryInfo &repository)
{
    if (repository.detached)
        throw GitError(QObject::tr("HEAD is detached: switch to a branch first"));
    if (repository.unborn)
        throw GitError(QObject::tr("The branch %1 has no commits yet").arg(repository.branch));
}

RepositoryInfo reread(const RepositoryInfo &repository)
{
    const std::optional<RepositoryInfo> info = RepositoryInfo::read(repository.workDir);
    if (!info)
        throw GitError(QObject::tr("The folder is not in a git repository"));
    return *info;
}

} // namespace

QStringList names(const QString &workDir)
{
    return QString::fromUtf8(Git::run(workDir, {QStringLiteral("remote")})).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QString defaultRemote(const QStringList &remotes)
{
    if (remotes.contains(QLatin1String("origin")))
        return QStringLiteral("origin");
    return remotes.size() == 1 ? remotes.first() : QString();
}

QString fetch(const RepositoryInfo &repository, const Git::Feedback &feedback)
{
    if (names(repository.workDir).isEmpty())
        throw GitError(QObject::tr("The repository has no remote"));
    Git::run(repository.workDir,
             {QStringLiteral("fetch"), QStringLiteral("--all"), QStringLiteral("--prune"), QStringLiteral("--progress")},
             options(feedback));
    const RepositoryInfo after = reread(repository);
    if (after.upstream.isEmpty() || after.behind == 0)
        return QObject::tr("Fetched: nothing new for %1").arg(after.branch.isEmpty() ? after.headId : after.branch);
    return after.behind == 1 ? QObject::tr("Fetched: 1 new commit on %1").arg(after.upstream)
                             : QObject::tr("Fetched: %1 new commits on %2").arg(after.behind).arg(after.upstream);
}

QString pull(const RepositoryInfo &repository, const Git::Feedback &feedback)
{
    requireBranch(repository);
    if (repository.upstream.isEmpty())
        throw GitError(QObject::tr("The branch %1 tracks no remote branch: push it first").arg(repository.branch));

    QStringList arguments{QStringLiteral("pull"), QStringLiteral("--progress")};
    // The user's choice between merge and rebase; none made: never a merge
    // commit nobody asked for.
    const bool configured = configValue(repository.workDir, QStringLiteral("pull.rebase"))
                            || configValue(repository.workDir, QStringLiteral("pull.ff"))
                            || configValue(repository.workDir, QStringLiteral("branch.%1.rebase").arg(repository.branch));
    if (!configured)
        arguments.append(QStringLiteral("--ff-only"));
    try {
        Git::run(repository.workDir, arguments, options(feedback));
    } catch (const GitError &error) {
        if (error.kind() == GitError::Kind::General
            && (error.message().contains(QLatin1String("Not possible to fast-forward"))
                || error.message().contains(QLatin1String("divergent branches")))) {
            throw GitError(QObject::tr("%1 and %2 have both moved on: merge or rebase to bring them together")
                               .arg(repository.branch, repository.upstream),
                           GitError::Kind::Diverged);
        }
        throw;
    }
    if (repository.behind == 1)
        return QObject::tr("Pulled 1 commit from %1").arg(repository.upstream);
    if (repository.behind > 1)
        return QObject::tr("Pulled %1 commits from %2").arg(repository.behind).arg(repository.upstream);
    return QObject::tr("Pulled from %1").arg(repository.upstream);
}

QString push(const RepositoryInfo &repository, const Git::Feedback &feedback, bool force)
{
    requireBranch(repository);
    QStringList arguments{QStringLiteral("push"), QStringLiteral("--progress")};
    if (force)
        arguments.append(QStringLiteral("--force-with-lease"));
    QString destination = repository.upstream;
    if (repository.upstream.isEmpty()) {
        const QStringList remotes = names(repository.workDir);
        const QString remote = defaultRemote(remotes);
        if (remote.isEmpty()) {
            throw GitError(remotes.isEmpty() ? QObject::tr("The repository has no remote")
                                             : QObject::tr("The branch %1 tracks no remote branch, and there is no "
                                                           "\"origin\" remote to push it to")
                                                   .arg(repository.branch));
        }
        arguments << QStringLiteral("--set-upstream") << remote << QStringLiteral("HEAD");
        destination = remote + QLatin1Char('/') + repository.branch;
    }
    try {
        Git::run(repository.workDir, arguments, options(feedback));
    } catch (const GitError &error) {
        if (error.kind() == GitError::Kind::General && error.message().contains(QLatin1String("[rejected]"))) {
            throw GitError(QObject::tr("%1 has commits that %2 lacks: pull them first, or overwrite them")
                               .arg(destination, repository.branch),
                           GitError::Kind::Rejected);
        }
        throw;
    }
    return QObject::tr("Pushed %1 to %2").arg(repository.branch, destination);
}

QString sync(const RepositoryInfo &repository, const Git::Feedback &feedback)
{
    requireBranch(repository);
    fetch(repository, feedback);
    RepositoryInfo current = reread(repository);
    QStringList done;
    if (!current.upstream.isEmpty() && current.behind > 0) {
        done.append(pull(current, feedback));
        current = reread(current);
    }
    if (current.upstream.isEmpty() || current.ahead > 0)
        done.append(push(current, feedback));
    return done.isEmpty() ? QObject::tr("%1 is up to date with %2").arg(current.branch, current.upstream)
                          : done.join(QStringLiteral(" · "));
}

} // namespace Remotes
