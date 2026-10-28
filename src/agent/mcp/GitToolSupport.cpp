// SPDX-License-Identifier: GPL-3.0-only

#include "GitToolSupport.h"

#include "McpWork.h"

#include <utility>

namespace GitToolSupport {

namespace {

constexpr int maximumPatch = 20000;

QString kindName(ChangeKind kind)
{
    switch (kind) {
    case ChangeKind::Added:
        return QStringLiteral("added");
    case ChangeKind::Modified:
        return QStringLiteral("modified");
    case ChangeKind::Deleted:
        return QStringLiteral("deleted");
    case ChangeKind::Renamed:
        return QStringLiteral("renamed");
    case ChangeKind::Conflicted:
        return QStringLiteral("conflicted");
    }
    return {};
}

using GitDone = std::function<void(const QString &message, const QString &error)>;

// Tells the editor the repository changed, then replies with the job's
// message or error.
GitDone replyAfterGit(const KDKToolContext &context, McpReply reply)
{
    return [context, reply = std::move(reply)](const QString &message, const QString &error) {
        if (context.repositoryChanged)
            context.repositoryChanged();
        reply(error.isEmpty() ? McpResult::message(message) : McpResult::error(error));
    };
}

} // namespace

// A patch as Claude reads it: cut beyond maximumPatch characters.
QString truncatedPatch(const QByteArray &bytes)
{
    QString patch = QString::fromUtf8(bytes);
    if (patch.size() > maximumPatch) {
        patch.truncate(maximumPatch);
        patch += QStringLiteral("\n[… cut]");
    }
    return patch;
}

QJsonArray diffsJson(const QList<FileDiff> &diffs, bool patches)
{
    QJsonArray array;
    for (const FileDiff &diff : diffs) {
        QJsonObject item{{QStringLiteral("path"), diff.change.path},
                         {QStringLiteral("change"), kindName(diff.change.kind)}};
        if (!diff.change.oldPath.isEmpty())
            item.insert(QStringLiteral("oldPath"), diff.change.oldPath);
        const bool hasPatch = patches && !diff.isTooLarge;
        if (hasPatch)
            item.insert(QStringLiteral("patch"), truncatedPatch(diff.patch));
        else if (patches)
            item.insert(QStringLiteral("tooLargeToDiff"), true);
        array.append(item);
    }
    return array;
}

QJsonObject commitJson(const CommitSummary &commit)
{
    return {{QStringLiteral("id"), commit.shortId},
            {QStringLiteral("summary"), commit.summary},
            {QStringLiteral("author"), commit.author},
            {QStringLiteral("date"), commit.date.toString(Qt::ISODate)},
            {QStringLiteral("refs"), QJsonArray::fromStringList(commit.refs)}};
}

// Runs a git job with the editor's other git work (one at a time, off the
// UI thread), then tells the editor the repository changed.
void runGit(const KDKToolContext &context, GitCommands::Job job, McpReply reply)
{
    const GitDone done = replyAfterGit(context, std::move(reply));
    if (context.git) {
        context.git->runQuietly(std::move(job), done);
        return;
    }
    const McpReply fromResult = [done](const McpResult &result) {
        done(result.isError ? QString() : result.text, result.isError ? result.text : QString());
    };
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks): the thread pool owns and deletes its runnable
    inBackground([job] { return McpResult::message(job()); }, fromResult);
}

// Same for a job that talks to a remote: git may ask the user for
// credentials (see AskpassServer).
void runRemoteGit(const KDKToolContext &context, GitCommands::LongJob job, McpReply reply)
{
    if (!context.git) {
        runGit(context, [job] { return job({}); }, std::move(reply));
        return;
    }
    context.git->runLongQuietly(std::move(job), replyAfterGit(context, std::move(reply)));
}

// "origin/main ↑2 ↓1" and the like, for get_git_status.

RepositoryInfo repositoryOf(const QString &folder)
{
    const std::optional<RepositoryInfo> repository = RepositoryInfo::read(folder);
    if (!repository)
        throw GitError(QStringLiteral("The project is not in a git repository"));
    return *repository;
}

QStringList pathsArgument(const KDKToolContext &context, const QJsonObject &arguments)
{
    QStringList paths;
    for (const QJsonValue &value : arguments.value(QStringLiteral("paths")).toArray())
        paths.append(context.relative(context.absolute(value.toString())));
    return paths;
}

// The "path" argument, relative to the project.
QString relativePathArgument(const KDKToolContext &context, const QJsonObject &arguments)
{
    return context.relative(context.absolute(arguments.value(QStringLiteral("path")).toString()));
}

} // namespace GitToolSupport
