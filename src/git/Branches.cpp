// SPDX-License-Identifier: GPL-3.0-only

#include "Branches.h"

#include "FileChange.h"
#include "GitProcess.h"
#include "Snapshots.h"
#include "Stashes.h"

#include <QObject>
#include <QRegularExpression>

namespace Branches {
namespace {

// Characters git refuses anywhere in a reference name.
bool isForbidden(QChar c)
{
    return c.unicode() < 0x20 || c.unicode() == 0x7f || QStringLiteral(" ~^:?*[\\").contains(c);
}

// "ahead 2, behind 1", "ahead 2", "gone", or empty when up to date.
void parseTracking(const QString &track, BranchInfo &branch)
{
    branch.upstreamGone = track == QLatin1String("gone");
    static const QRegularExpression counts(QStringLiteral(R"((ahead|behind) (\d+))"));
    for (auto it = counts.globalMatch(track); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        (match.captured(1) == QLatin1String("ahead") ? branch.ahead : branch.behind) = match.captured(2).toInt();
    }
}

// "git switch" to an existing local branch, or to a commit with --detach.
void runSwitch(const RepositoryInfo &repository, const QStringList &arguments)
{
    try {
        Git::run(repository.workDir, QStringList{QStringLiteral("switch")} + arguments, {}, Git::englishMessages());
    } catch (const GitError &error) {
        if (error.message().contains(QLatin1String("would be overwritten")))
            throw GitError(error.message(), GitError::Kind::LocalChanges);
        throw;
    }
}

// The arguments of "git switch" that make `name` the current branch.
QStringList switchArguments(const RepositoryInfo &repository, const QString &name, bool remote)
{
    if (!remote)
        return {name};
    const QString localName = nameOnRemote(name);
    if (local(repository.workDir).contains(localName))
        return {localName};
    return {QStringLiteral("--track"), name};
}

// Why `name` holds a character git refuses, or empty.
QString characterError(const QString &name)
{
    for (const QChar c : name) {
        if (c == QLatin1Char(' '))
            return QObject::tr("A branch name cannot contain spaces.");
        if (!isForbidden(c))
            continue;
        const QString shown = c.unicode() < 0x20 ? QStringLiteral("\\t") : QString(c);
        return QObject::tr("A branch name cannot contain \"%1\".").arg(shown);
    }
    return {};
}

// Why git refuses the name as a whole, or empty.
QString wholeNameError(const QString &name)
{
    if (name.startsWith(QLatin1Char('-')))
        return QObject::tr("A branch name cannot start with \"-\".");
    if (name == QLatin1String("@"))
        return QObject::tr("\"@\" cannot name a branch.");
    if (name.contains(QLatin1String("..")))
        return QObject::tr("A branch name cannot contain \"..\".");
    if (name.contains(QLatin1String("@{")))
        return QObject::tr("A branch name cannot contain \"@{\".");
    if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char('/')))
        return QObject::tr("A branch name cannot end with \"%1\".").arg(name.right(1));
    return {};
}

// Why git refuses one "/"-separated part of a name, or empty.
QString partError(const QString &part)
{
    if (part.isEmpty())
        return QObject::tr("A branch name cannot contain \"//\" or start with \"/\".");
    if (part.startsWith(QLatin1Char('.')))
        return QObject::tr("A part of a branch name cannot start with \".\".");
    if (part.endsWith(QLatin1String(".lock")))
        return QObject::tr("A part of a branch name cannot end with \".lock\".");
    return {};
}

} // namespace

QString nameError(const QString &name)
{
    if (name.isEmpty())
        return QObject::tr("Enter a name.");
    if (const QString error = characterError(name); !error.isEmpty())
        return error;
    if (const QString error = wholeNameError(name); !error.isEmpty())
        return error;
    for (const QString &part : name.split(QLatin1Char('/'))) {
        if (const QString error = partError(part); !error.isEmpty())
            return error;
    }
    return {};
}

QString sanitizedName(const QString &typed)
{
    QString name;
    for (const QChar c : typed) {
        if (c == QLatin1Char(' '))
            name += QLatin1Char('-');
        else if (!isForbidden(c))
            name += c;
    }
    // Runs of "-" from several spaces, and ".." which git refuses.
    static const QRegularExpression dashes(QStringLiteral("-{2,}"));
    name.replace(dashes, QStringLiteral("-"));
    while (name.contains(QLatin1String("..")))
        name.replace(QLatin1String(".."), QLatin1String("."));
    return name;
}

QStringList local(const QString &workDir)
{
    const QByteArray output =
        Git::run(workDir, {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname:short)"),
                           QStringLiteral("refs/heads")});
    return QString::fromUtf8(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QList<BranchInfo> list(const QString &workDir)
{
    const QByteArray output =
        Git::run(workDir, {QStringLiteral("for-each-ref"),
                           QStringLiteral("--format=%(refname)%00%(refname:short)%00%(HEAD)%00%(upstream:short)%00"
                                          "%(upstream:track,nobracket)%00%(committerdate:unix)%00%(contents:subject)"),
                           QStringLiteral("refs/heads"), QStringLiteral("refs/remotes")});
    QList<BranchInfo> branches;
    for (const QByteArray &line : output.split('\n')) {
        const QList<QByteArray> fields = line.split('\0');
        if (fields.size() < 7)
            continue;
        const QString ref = QString::fromUtf8(fields.at(0));
        // origin/HEAD: the remote's default branch, not a branch.
        if (ref.endsWith(QLatin1String("/HEAD")))
            continue;
        BranchInfo branch;
        branch.name = QString::fromUtf8(fields.at(1));
        branch.remote = ref.startsWith(QLatin1String("refs/remotes/"));
        branch.current = fields.at(2) == "*";
        branch.upstream = QString::fromUtf8(fields.at(3));
        parseTracking(QString::fromUtf8(fields.at(4)), branch);
        branch.date = QDateTime::fromSecsSinceEpoch(fields.at(5).toLongLong());
        branch.subject = QString::fromUtf8(fields.at(6));
        branches.append(branch);
    }
    // for-each-ref sorts by reference: refs/heads before refs/remotes.
    return branches;
}

void switchTo(const RepositoryInfo &repository, const QString &name, bool remote)
{
    Snapshots::createIfChanged(repository, QObject::tr("Before switching to %1").arg(name));
    runSwitch(repository, switchArguments(repository, name, remote));
}

void stashAndSwitch(const RepositoryInfo &repository, const QString &name, bool remote)
{
    const QStringList arguments = switchArguments(repository, name, remote);
    Snapshots::createIfChanged(repository, QObject::tr("Before switching to %1").arg(name));
    Stashes::push(repository, QObject::tr("Changes brought to %1").arg(arguments.last()), true);
    try {
        runSwitch(repository, arguments);
    } catch (const GitError &) {
        // Back to where we were.
        Stashes::pop(repository, 0);
        throw;
    }
    try {
        Stashes::pop(repository, 0);
    } catch (const GitError &error) {
        throw GitError(QObject::tr("Switched to %1, but the changes brought along conflict with it: resolve the "
                                   "conflicts, then drop the stash where they are still kept.\n\n%2")
                           .arg(arguments.last(), error.message()));
    }
}

void detach(const RepositoryInfo &repository, const QString &id)
{
    Snapshots::createIfChanged(repository, QObject::tr("Before checking out %1").arg(id));
    runSwitch(repository, {QStringLiteral("--detach"), id});
}

void rename(const RepositoryInfo &repository, const QString &name, const QString &newName)
{
    Git::run(repository.workDir,
             {QStringLiteral("branch"), QStringLiteral("--move"), QStringLiteral("--"), name, newName});
}

void remove(const RepositoryInfo &repository, const QString &name, bool force)
{
    try {
        Git::run(
            repository.workDir,
            {QStringLiteral("branch"), force ? QStringLiteral("-D") : QStringLiteral("-d"), QStringLiteral("--"), name},
            {}, Git::englishMessages());
    } catch (const GitError &error) {
        if (error.message().contains(QLatin1String("not fully merged"))) {
            throw GitError(QObject::tr("The branch %1 has commits that are in no other branch").arg(name),
                           GitError::Kind::Unmerged);
        }
        // "used by worktree at '<folder>'"; "checked out at" before git 2.42.
        static const QRegularExpression inWorktree(QStringLiteral("(?:used by worktree|checked out) at '(.+)'"));
        if (const QRegularExpressionMatch match = inWorktree.match(error.message()); match.hasMatch()) {
            throw GitError(QObject::tr("The branch %1 is checked out in the worktree %2").arg(name, match.captured(1)),
                           GitError::Kind::InWorktree);
        }
        throw;
    }
}

void removeRemote(const RepositoryInfo &repository, const QString &name, const Git::Feedback &feedback)
{
    Git::run(repository.workDir,
             {QStringLiteral("push"), QStringLiteral("--progress"), remoteOf(name), QStringLiteral("--delete"),
              nameOnRemote(name)},
             Git::RunOptions{{}, {}, feedback});
}

void setUpstream(const RepositoryInfo &repository, const QString &name, const QString &upstream)
{
    Git::run(repository.workDir, {QStringLiteral("branch"), QStringLiteral("--set-upstream-to=") + upstream, name});
}

QString remoteOf(const QString &remoteBranch)
{
    return remoteBranch.section(QLatin1Char('/'), 0, 0);
}

QString nameOnRemote(const QString &remoteBranch)
{
    return remoteBranch.section(QLatin1Char('/'), 1);
}

void create(const RepositoryInfo &repository, const QString &name, bool switchTo)
{
    if (switchTo)
        Git::run(repository.workDir, {QStringLiteral("switch"), QStringLiteral("--create"), name});
    else
        Git::run(repository.workDir, {QStringLiteral("branch"), QStringLiteral("--"), name});
}

} // namespace Branches
