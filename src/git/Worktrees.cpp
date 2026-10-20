// SPDX-License-Identifier: GPL-3.0-only

#include "Worktrees.h"

#include "Branches.h"
#include "FileChange.h"
#include "GitProcess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

namespace Worktrees {
namespace {

const QString branchPrefix = QStringLiteral("duet/");
const QString namePrefix = QStringLiteral("agent-");
// The exclusion line, relative to the repository root.
const QByteArray excludeLine = QByteArrayLiteral("/.claude/worktrees/");

// Adds the worktrees' folder to .git/info/exclude unless it is there already.
void excludeFolder(const RepositoryInfo &repository)
{
    // --git-path finds the shared file even when the checkout is itself a
    // secondary worktree.
    const QString relative =
        QString::fromUtf8(Git::run(repository.workDir, {QStringLiteral("rev-parse"), QStringLiteral("--git-path"),
                                                        QStringLiteral("info/exclude")})
                              .trimmed());
    const QString path = QDir(repository.workDir).absoluteFilePath(relative);
    QFile file(path);
    const QByteArray content = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    file.close();
    if (content.split('\n').contains(excludeLine))
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!file.open(QIODevice::Append))
        throw GitError(QObject::tr("Could not add the Claude worktrees to %1: %2").arg(path, file.errorString()));
    const bool endsWithNewline = content.isEmpty() || content.endsWith('\n');
    file.write((endsWithNewline ? QByteArray() : QByteArrayLiteral("\n")) + excludeLine + '\n');
}

bool hasChanges(const Worktree &worktree)
{
    return !Git::run(worktree.path, {QStringLiteral("status"), QStringLiteral("--porcelain")}).trimmed().isEmpty();
}

// The files a stopped rebase left with conflicts.
QStringList conflictedFiles(const Worktree &worktree)
{
    return Git::splitNul(Git::run(worktree.path, {QStringLiteral("diff"), QStringLiteral("--name-only"),
                                                  QStringLiteral("--diff-filter=U"), QStringLiteral("-z")}));
}

} // namespace

QString folder(const RepositoryInfo &repository)
{
    return QDir(repository.workDir).filePath(QStringLiteral(".claude/worktrees"));
}

QString unusedName(const RepositoryInfo &repository)
{
    const QStringList branches = Branches::local(repository.workDir);
    const QDir worktrees(folder(repository));
    for (int number = 1;; ++number) {
        const QString name = namePrefix + QString::number(number);
        if (!worktrees.exists(name) && !branches.contains(branchPrefix + name))
            return name;
    }
}

Worktree add(const RepositoryInfo &repository, const QString &name)
{
    if (const QString error = Branches::nameError(branchPrefix + name); !error.isEmpty())
        throw GitError(error);
    excludeFolder(repository);
    Worktree worktree{
        .name = name,
        .path = QDir(folder(repository)).filePath(name),
        .branch = branchPrefix + name,
    };
    Git::run(repository.workDir, {QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("-b"),
                                  worktree.branch, worktree.path, QStringLiteral("HEAD")});
    worktree.baseCommit = head(worktree);
    return worktree;
}

bool hasWork(const Worktree &worktree)
{
    return hasChanges(worktree) || hasCommitsToMerge(worktree);
}

bool hasCommitsToMerge(const Worktree &worktree)
{
    const QByteArray count = Git::run(worktree.path, {QStringLiteral("rev-list"), QStringLiteral("--count"),
                                                      worktree.baseCommit + QStringLiteral("..HEAD")});
    return count.trimmed().toInt() > 0;
}

void commitChanges(const Worktree &worktree, const QString &message)
{
    if (!hasChanges(worktree))
        return;
    Git::run(worktree.path, {QStringLiteral("add"), QStringLiteral("-A")});
    Git::run(worktree.path, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-F"), QStringLiteral("-")},
             message.toUtf8());
}

RebaseResult rebase(const Worktree &worktree, const QString &onto)
{
    try {
        Git::run(worktree.path, {QStringLiteral("rebase"), QStringLiteral("--autostash"), onto}, {},
                 Git::englishMessages());
        return {};
    } catch (const GitError &) {
        const QStringList conflicts = conflictedFiles(worktree);
        if (conflicts.isEmpty())
            throw;
        Git::run(worktree.path, {QStringLiteral("rebase"), QStringLiteral("--abort")});
        return {.conflicts = conflicts};
    }
}

void fastForward(const RepositoryInfo &repository, const QString &branch)
{
    if (repository.detached)
        throw GitError(QObject::tr("The checkout has no current branch to merge %1 into.").arg(branch));
    try {
        Git::run(repository.workDir, {QStringLiteral("merge"), QStringLiteral("--ff-only"), branch}, {},
                 Git::englishMessages());
    } catch (const GitError &error) {
        if (error.message().contains(QLatin1String("would be overwritten")))
            throw GitError(error.message(), GitError::Kind::LocalChanges);
        if (error.message().contains(QLatin1String("Not possible to fast-forward")))
            throw GitError(error.message(), GitError::Kind::Diverged);
        throw;
    }
}

QString head(const Worktree &worktree)
{
    return QString::fromUtf8(Git::run(worktree.path, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}).trimmed());
}

void remove(const RepositoryInfo &repository, const Worktree &worktree)
{
    Git::run(repository.workDir,
             {QStringLiteral("worktree"), QStringLiteral("remove"), QStringLiteral("--force"), worktree.path});
    Branches::remove(repository, worktree.branch, true);
}

} // namespace Worktrees
