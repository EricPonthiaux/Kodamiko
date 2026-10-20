// SPDX-License-Identifier: GPL-3.0-only

#include "Worktrees.h"

#include "Branches.h"
#include "FileChange.h"
#include "GitProcess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#include <algorithm>

namespace Worktrees {
namespace {

const QString branchPrefix = QStringLiteral("duet/");
const QString namePrefix = QStringLiteral("agent-");
// The exclusion line, relative to the repository root.
const QByteArray excludeLine = QByteArrayLiteral("/.claude/worktrees/");
// The lines of `git worktree list --porcelain` read here.
const QByteArray folderAttribute = QByteArrayLiteral("worktree ");
const QByteArray branchAttribute = QByteArrayLiteral("branch refs/heads/");

// A worktree as `git worktree list` describes it.
struct ListedWorktree {
    QString folder;
    QString branch; // "duet/agent-1"; empty when detached
};

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

// The repository's worktrees, its own checkout first.
QList<ListedWorktree> listed(const RepositoryInfo &repository)
{
    // An attribute per line, ended by a NUL with -z.
    const QByteArray output = Git::run(repository.workDir, {QStringLiteral("worktree"), QStringLiteral("list"),
                                                            QStringLiteral("--porcelain"), QStringLiteral("-z")});
    QList<ListedWorktree> worktrees;
    for (const QByteArray &line : output.split('\0')) {
        if (line.startsWith(folderAttribute))
            worktrees.append({.folder = QString::fromUtf8(line.mid(folderAttribute.size()))});
        else if (line.startsWith(branchAttribute) && !worktrees.isEmpty())
            worktrees.last().branch = QString::fromUtf8(line.mid(branchAttribute.size()));
    }
    return worktrees;
}

bool isSameFolder(const QString &folder, const QString &other)
{
    return QDir::cleanPath(folder) == QDir::cleanPath(other);
}

bool isListed(const RepositoryInfo &repository, const QString &folder)
{
    return std::ranges::any_of(listed(repository), [&folder](const ListedWorktree &worktree) {
        return isSameFolder(worktree.folder, folder);
    });
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
    // Its conflicts would be taken for the replay's, and undone with it.
    const std::optional<RepositoryInfo> state = RepositoryInfo::read(worktree.path);
    if (state && !state->operation.isEmpty())
        return {.pendingOperation = state->operation};
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

QStringList blockingFiles(const RepositoryInfo &repository, const QString &mergeError)
{
    // git lists them one per line, indented by a tab, under "Your local
    // changes to the following files would be overwritten by merge:" or
    // "The following untracked working tree files would be overwritten…".
    QStringList files;
    const QDir root(repository.workDir);
    for (const QString &line : mergeError.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1Char('\t')))
            files.append(root.filePath(line.trimmed()));
    }
    return files;
}

QString head(const Worktree &worktree)
{
    return QString::fromUtf8(Git::run(worktree.path, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}).trimmed());
}

std::optional<QString> folderHolding(const RepositoryInfo &repository, const QString &branch)
{
    for (const ListedWorktree &worktree : listed(repository)) {
        const bool isOther = !isSameFolder(worktree.folder, repository.workDir);
        if (isOther && worktree.branch == branch)
            return worktree.folder;
    }
    return std::nullopt;
}

void removeFolder(const RepositoryInfo &repository, const QString &folder)
{
    // Forgets the worktrees whose folder was deleted by hand.
    Git::run(repository.workDir, {QStringLiteral("worktree"), QStringLiteral("prune")});
    if (!QFileInfo::exists(folder) && !isListed(repository, folder))
        return;
    // Forced twice: uncommitted changes and a lock do not keep it.
    Git::run(repository.workDir, {QStringLiteral("worktree"), QStringLiteral("remove"), QStringLiteral("--force"),
                                  QStringLiteral("--force"), folder});
    // Its branch cannot go while git still lists it: say so, rather than
    // git's "cannot delete branch used by worktree".
    if (isListed(repository, folder))
        throw GitError(QObject::tr("git did not remove the worktree %1").arg(folder));
}

void remove(const RepositoryInfo &repository, const Worktree &worktree)
{
    removeFolder(repository, worktree.path);
    if (Branches::local(repository.workDir).contains(worktree.branch))
        Branches::remove(repository, worktree.branch, true);
}

} // namespace Worktrees
