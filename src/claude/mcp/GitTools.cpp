// MCP tools for git and Duet's snapshots.

#include "DuetTools.h"
#include "McpRegistry.h"
#include "McpWork.h"

#include "git/CommitLog.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"
#include "git/RepositoryStatus.h"
#include "git/Snapshots.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace DuetTools {

namespace {

using namespace McpSchema;

constexpr int maximumPatch = 20000;

QString kindName(ChangeKind kind)
{
    switch (kind) {
    case ChangeKind::Added: return QStringLiteral("added");
    case ChangeKind::Modified: return QStringLiteral("modified");
    case ChangeKind::Deleted: return QStringLiteral("deleted");
    case ChangeKind::Renamed: return QStringLiteral("renamed");
    case ChangeKind::Conflicted: return QStringLiteral("conflicted");
    }
    return {};
}

QJsonArray diffsJson(const QList<FileDiff> &diffs, bool patches)
{
    QJsonArray array;
    for (const FileDiff &diff : diffs) {
        QJsonObject item{{QStringLiteral("path"), diff.change.path}, {QStringLiteral("change"), kindName(diff.change.kind)}};
        if (!diff.change.oldPath.isEmpty())
            item.insert(QStringLiteral("oldPath"), diff.change.oldPath);
        if (patches) {
            QString patch = QString::fromUtf8(diff.patch);
            if (patch.size() > maximumPatch) {
                patch.truncate(maximumPatch);
                patch += QStringLiteral("\n[… cut]");
            }
            item.insert(QStringLiteral("patch"), patch);
        }
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
void runGit(const DuetToolContext &context, GitCommands::Job job, McpReply reply)
{
    const auto done = [context, reply](const QString &message, const QString &error) {
        if (context.repositoryChanged)
            context.repositoryChanged();
        reply(error.isEmpty() ? McpResult::message(message) : McpResult::error(error));
    };
    if (context.git) {
        context.git->runQuietly(std::move(job), done);
        return;
    }
    inBackground([job] { return McpResult::message(job()); },
                 [done](const McpResult &result) { done(result.isError ? QString() : result.text,
                                                        result.isError ? result.text : QString()); });
}

RepositoryInfo repositoryOf(const QString &folder)
{
    const std::optional<RepositoryInfo> repository = RepositoryInfo::read(folder);
    if (!repository)
        throw GitError(QStringLiteral("The project is not in a git repository"));
    return *repository;
}

QStringList pathsArgument(const DuetToolContext &context, const QJsonObject &arguments)
{
    QStringList paths;
    for (const QJsonValue &value : arguments.value(QStringLiteral("paths")).toArray())
        paths.append(context.relative(context.absolute(value.toString())));
    return paths;
}

} // namespace

void registerGitTools(McpRegistry &registry, const DuetToolContext &context)
{
    registry.addTool(McpTool::reading(
        QStringLiteral("get_git_status"), QStringLiteral("Git status"),
        QStringLiteral("The project's git state as Duet's Diff mode shows it: branch, last commit, operation in "
                       "progress, staged and unstaged changes (untracked files included), optionally with their "
                       "patches."),
        object({{QStringLiteral("patches"), boolean(QStringLiteral("Include each file's unified diff"))}}),
        [context](const QJsonObject &arguments, McpReply reply, McpProgress) {
            const QString folder = context.folder();
            const bool patches = arguments.value(QStringLiteral("patches")).toBool();
            inBackground(
                [folder, patches] {
                    const std::optional<RepositoryStatus> status = RepositoryStatus::read(folder);
                    if (!status)
                        return McpResult::error(QStringLiteral("The project is not in a git repository"));
                    const RepositoryInfo &info = status->info;
                    return McpResult::json(QJsonObject{
                        {QStringLiteral("branch"), info.detached ? QString() : info.branch},
                        {QStringLiteral("detached"), info.detached},
                        {QStringLiteral("head"), info.headId},
                        {QStringLiteral("headMessage"), info.headMessage},
                        {QStringLiteral("operation"), info.operation},
                        {QStringLiteral("staged"), diffsJson(status->staged, patches)},
                        {QStringLiteral("unstaged"), diffsJson(status->unstaged, patches)}});
                },
                reply);
        }));

    registry.addTool(McpTool::reading(
        QStringLiteral("get_file_history"), QStringLiteral("History"),
        QStringLiteral("The commits of the project, or of one file or folder, newest first, as Duet's History "
                       "mode shows them."),
        object({{QStringLiteral("path"), string(QStringLiteral("File or folder (optional): its commits only"))},
                {QStringLiteral("limit"), integer(QStringLiteral("Most commits (default 30)"))}}),
        [context](const QJsonObject &arguments, McpReply reply, McpProgress) {
            const QString folder = context.folder();
            const QString path = arguments.contains(QStringLiteral("path"))
                                     ? context.relative(context.absolute(arguments.value(QStringLiteral("path")).toString()))
                                     : QString();
            const int limit = std::clamp(arguments.value(QStringLiteral("limit")).toInt(30), 1, 500);
            inBackground(
                [folder, path, limit] {
                    const std::optional<CommitLog> log = CommitLog::read(folder, limit, path);
                    if (!log)
                        return McpResult::error(QStringLiteral("The project is not in a git repository"));
                    QJsonArray commits;
                    for (const CommitSummary &commit : log->commits)
                        commits.append(commitJson(commit));
                    return McpResult::json(QJsonObject{{QStringLiteral("commits"), commits},
                                                       {QStringLiteral("truncated"), log->truncated}});
                },
                reply);
        }));

    registry.addTool(McpTool::reading(
        QStringLiteral("get_commit"), QStringLiteral("Commit"),
        QStringLiteral("A commit's message, author, parents and changed files, optionally with their patches."),
        object({{QStringLiteral("id"), string(QStringLiteral("Commit id (abbreviated or full)"))},
                {QStringLiteral("patches"), boolean(QStringLiteral("Include each file's unified diff"))}},
               {QStringLiteral("id")}),
        [context](const QJsonObject &arguments, McpReply reply, McpProgress) {
            const QString folder = context.folder();
            const QString id = arguments.value(QStringLiteral("id")).toString();
            const bool patches = arguments.value(QStringLiteral("patches")).toBool();
            inBackground(
                [folder, id, patches] {
                    const CommitDetails details = CommitDetails::read(folder, id);
                    QJsonObject json = commitJson(details.summary);
                    json.insert(QStringLiteral("message"), details.message);
                    json.insert(QStringLiteral("parents"), QJsonArray::fromStringList(details.parentIds));
                    json.insert(QStringLiteral("files"), diffsJson(details.files, patches));
                    return McpResult::json(json);
                },
                reply);
        }));

    registry.addTool(McpTool::reading(
        QStringLiteral("list_snapshots"), QStringLiteral("Snapshots"),
        QStringLiteral("Duet's snapshots of the working directory, newest first: taken before each prompt and on "
                       "request, they can bring files back to an earlier state (restore_snapshot)."),
        object({}),
        [context](const QJsonObject &, McpReply reply, McpProgress) {
            const QString folder = context.folder();
            inBackground(
                [folder] {
                    QJsonArray snapshots;
                    for (const Snapshot &snapshot : Snapshots::list(folder))
                        snapshots.append(QJsonObject{{QStringLiteral("id"), snapshot.commitId.left(10)},
                                                     {QStringLiteral("date"), snapshot.date.toString(Qt::ISODate)},
                                                     {QStringLiteral("reason"), snapshot.reason},
                                                     {QStringLiteral("wholeWorkingTree"), snapshot.wholeWorkingTree},
                                                     {QStringLiteral("files"), QJsonArray::fromStringList(snapshot.paths)}});
                    return McpResult::json(QJsonObject{{QStringLiteral("snapshots"), snapshots}});
                },
                reply);
        }));

    registry.addResource({
        QStringLiteral("duet://git/status"),
        false,
        QStringLiteral("git-status"),
        QStringLiteral("Git status"),
        QStringLiteral("Branch, last commit, staged and unstaged files"),
        QStringLiteral("application/json"),
        [context](const QString &) -> std::optional<QString> {
            try {
                const std::optional<RepositoryStatus> status = RepositoryStatus::read(context.folder());
                if (!status)
                    return QStringLiteral("{}");
                return QString::fromUtf8(QJsonDocument(QJsonObject{
                                                           {QStringLiteral("branch"), status->info.branch},
                                                           {QStringLiteral("head"), status->info.headId},
                                                           {QStringLiteral("staged"), diffsJson(status->staged, false)},
                                                           {QStringLiteral("unstaged"), diffsJson(status->unstaged, false)}})
                                             .toJson(QJsonDocument::Compact));
            } catch (const GitError &) {
                return std::nullopt;
            }
        },
    });

    registry.addTool(McpTool::acting(
        QStringLiteral("create_snapshot"), QStringLiteral("Take a snapshot"),
        QStringLiteral("Records the working directory's files in a Duet snapshot (outside any branch), to come "
                       "back to them later with restore_snapshot: before a risky change."),
        object({{QStringLiteral("reason"), string(QStringLiteral("What the snapshot is before, shown to the user"))}},
               {QStringLiteral("reason")}),
        [context](const QJsonObject &arguments, McpReply reply, McpProgress) {
            const QString folder = context.folder();
            const QString reason = arguments.value(QStringLiteral("reason")).toString();
            runGit(context, [folder, reason] {
                const QString id = Snapshots::createOfWorkingTree(repositoryOf(folder), reason);
                return id.isEmpty() ? QStringLiteral("Nothing to record: the files are as committed")
                                    : QStringLiteral("Snapshot %1 taken").arg(id.left(10));
            }, reply);
        }));

    registry.addTool(McpTool::sensitive(
        QStringLiteral("restore_snapshot"), QStringLiteral("Restore a snapshot"),
        QStringLiteral("Brings the files of a Duet snapshot back on disk (see list_snapshots), the current ones "
                       "being saved first so that the restore can itself be undone. The user is asked first."),
        object({{QStringLiteral("id"), string(QStringLiteral("Snapshot id (see list_snapshots)"))}},
               {QStringLiteral("id")}),
        [](const QJsonObject &arguments) {
            return QStringLiteral("restore the files of snapshot %1").arg(arguments.value(QStringLiteral("id")).toString());
        },
        [context](const QJsonObject &arguments, McpReply reply, McpProgress) {
            const QString folder = context.folder();
            const QString id = arguments.value(QStringLiteral("id")).toString();
            runGit(context, [folder, id] {
                for (const Snapshot &snapshot : Snapshots::list(folder)) {
                    if (!id.isEmpty() && snapshot.commitId.startsWith(id)) {
                        const int count = Snapshots::restore(repositoryOf(folder), snapshot);
                        return QStringLiteral("%1 file(s) restored").arg(count);
                    }
                }
                throw GitError(QStringLiteral("No snapshot %1 (see list_snapshots)").arg(id));
            }, reply);
        }));

    registry.addTool(McpTool::acting(
        QStringLiteral("show_changes"), QStringLiteral("Show changes"),
        QStringLiteral("Shows Duet's Diff mode to the user, on a file's changes if given: for them to review what "
                       "changed."),
        object({{QStringLiteral("path"), string(QStringLiteral("File to show (optional)"))}}),
        McpTool::immediate([context](const QJsonObject &arguments) {
            if (!context.showChanges)
                return McpResult::error(QStringLiteral("Not available"));
            const QString path = arguments.value(QStringLiteral("path")).toString();
            context.showChanges(path.isEmpty() ? QString() : context.relative(context.absolute(path)));
            return McpResult::message(QStringLiteral("Shown"));
        })));

    registry.addTool(McpTool::sensitive(
        QStringLiteral("stage_files"), QStringLiteral("Stage files"),
        QStringLiteral("Adds files' changes to git's index (or removes them from it with unstage), as Duet's Diff "
                       "mode does. The user is asked first."),
        object({{QStringLiteral("paths"), array(string(QStringLiteral("File")), QStringLiteral("Files"))},
                {QStringLiteral("unstage"), boolean(QStringLiteral("Remove from the index instead"))}},
               {QStringLiteral("paths")}),
        [context](const QJsonObject &arguments) {
            return QStringLiteral("%1 %2").arg(arguments.value(QStringLiteral("unstage")).toBool() ? QStringLiteral("unstage")
                                                                                                    : QStringLiteral("stage"),
                                               pathsArgument(context, arguments).join(QStringLiteral(", ")));
        },
        [context](const QJsonObject &arguments, McpReply reply, McpProgress) {
            const QString folder = context.folder();
            const QStringList paths = pathsArgument(context, arguments);
            const bool unstage = arguments.value(QStringLiteral("unstage")).toBool();
            runGit(context, [folder, paths, unstage] {
                const RepositoryInfo repository = repositoryOf(folder);
                if (unstage)
                    GitOperations::unstage(repository, paths);
                else
                    GitOperations::stage(repository, paths);
                return QStringLiteral("%1 file(s) %2").arg(paths.size()).arg(unstage ? QStringLiteral("unstaged")
                                                                                     : QStringLiteral("staged"));
            }, reply);
        }));

    registry.addTool(McpTool::sensitive(
        QStringLiteral("commit"), QStringLiteral("Commit"),
        QStringLiteral("Commits what is staged with this message (amending the last commit if asked). The user is "
                       "asked first, with the message."),
        object({{QStringLiteral("message"), string(QStringLiteral("Commit message"))},
                {QStringLiteral("amend"), boolean(QStringLiteral("Amend the last commit"))}},
               {QStringLiteral("message")}),
        [](const QJsonObject &arguments) {
            return QStringLiteral("%1 \u201c%2\u201d")
                .arg(arguments.value(QStringLiteral("amend")).toBool() ? QStringLiteral("amend the last commit with")
                                                                       : QStringLiteral("commit"),
                     arguments.value(QStringLiteral("message")).toString().section(QLatin1Char('\n'), 0, 0));
        },
        [context](const QJsonObject &arguments, McpReply reply, McpProgress) {
            const QString folder = context.folder();
            const QString message = arguments.value(QStringLiteral("message")).toString();
            const bool amend = arguments.value(QStringLiteral("amend")).toBool();
            runGit(context, [folder, message, amend] {
                return GitOperations::commit(repositoryOf(folder), message, amend);
            }, reply);
        }));
}

} // namespace DuetTools
