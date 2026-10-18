#include "Stashes.h"

#include "FileChange.h"
#include "GitProcess.h"

#include <QObject>

namespace Stashes {
namespace {

QString reference(int index)
{
    return QStringLiteral("stash@{%1}").arg(index);
}

// Runs a stash command whose failure may come from local changes.
void runReporting(const QString &workDir, const QStringList &arguments)
{
    try {
        Git::run(workDir, arguments, {}, Git::englishMessages());
    } catch (const GitError &error) {
        if (error.message().contains(QLatin1String("would be overwritten")))
            throw GitError(error.message(), GitError::Kind::LocalChanges);
        throw;
    }
}

} // namespace

QList<Stash> list(const QString &workDir)
{
    // %gd: stash@{n}; one stash per line, fields separated by a null byte.
    const QByteArray output = Git::run(
        workDir, {QStringLiteral("stash"), QStringLiteral("list"), QStringLiteral("--format=%gd%x00%gs%x00%ct")});
    QList<Stash> stashes;
    for (const QByteArray &line : output.split('\n')) {
        const QList<QByteArray> fields = line.split('\0');
        if (fields.size() < 3)
            continue;
        Stash stash;
        const QString name = QString::fromUtf8(fields.at(0)); // "stash@{3}"
        stash.index = name.mid(7, name.size() - 8).toInt();
        stash.message = QString::fromUtf8(fields.at(1));
        stash.date = QDateTime::fromSecsSinceEpoch(fields.at(2).toLongLong());
        stashes.append(stash);
    }
    return stashes;
}

void push(const RepositoryInfo &repository, const QString &message, bool includeUntracked)
{
    QStringList arguments{QStringLiteral("stash"), QStringLiteral("push")};
    if (includeUntracked)
        arguments.append(QStringLiteral("--include-untracked"));
    if (!message.isEmpty())
        arguments << QStringLiteral("--message") << message;
    // With literal pathspecs, git's own ":/" (the whole tree) would name a
    // file: the untracked files would be stashed but left on disk.
    QHash<QString, QString> environment = Git::englishMessages();
    environment.insert(QStringLiteral("GIT_LITERAL_PATHSPECS"), QStringLiteral("0"));
    // "No local changes to save" is not an error for git.
    const QString output = QString::fromUtf8(Git::run(repository.workDir, arguments, {}, environment));
    if (output.contains(QLatin1String("No local changes to save")))
        throw GitError(QObject::tr("There are no changes to stash"));
}

void apply(const RepositoryInfo &repository, int index)
{
    runReporting(repository.workDir, {QStringLiteral("stash"), QStringLiteral("apply"), reference(index)});
}

void pop(const RepositoryInfo &repository, int index)
{
    runReporting(repository.workDir, {QStringLiteral("stash"), QStringLiteral("pop"), reference(index)});
}

void drop(const RepositoryInfo &repository, int index)
{
    Git::run(repository.workDir, {QStringLiteral("stash"), QStringLiteral("drop"), reference(index)});
}

QByteArray patch(const QString &workDir, int index)
{
    return Git::run(workDir, {QStringLiteral("stash"), QStringLiteral("show"), QStringLiteral("--patch"),
                              QStringLiteral("--include-untracked"), QStringLiteral("--no-color"), reference(index)});
}

} // namespace Stashes
