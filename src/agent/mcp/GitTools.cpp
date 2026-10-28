// SPDX-License-Identifier: GPL-3.0-only

// MCP tools for git's state, history, index and commits, and Kodamiko's
// snapshots.

#include "GitToolSupport.h"
#include "KDKTools.h"
#include "McpRegistry.h"
#include "McpWork.h"
#include "agent/CommitMessage.h"
#include "git/CommitLog.h"
#include "git/GitOperations.h"
#include "git/RepositoryStatus.h"
#include "git/Snapshots.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace KDKTools {

namespace {

using namespace McpSchema;
using namespace GitToolSupport;

void addTracking(QJsonObject &json, const RepositoryInfo &info)
{
    json.insert(QStringLiteral("upstream"), info.upstream);
    json.insert(QStringLiteral("ahead"), info.ahead);
    json.insert(QStringLiteral("behind"), info.behind);
}

McpResult gitStatus(const QString &folder, bool patches)
{
    const std::optional<RepositoryStatus> status = RepositoryStatus::read(folder);
    if (!status)
        return McpResult::error(QStringLiteral("The project is not in a git repository"));
    const RepositoryInfo &info = status->info;
    QJsonObject json{{QStringLiteral("branch"), info.detached ? QString() : info.branch},
                     {QStringLiteral("detached"), info.detached},
                     {QStringLiteral("head"), info.headId},
                     {QStringLiteral("headMessage"), info.headMessage},
                     {QStringLiteral("operation"), info.operation},
                     {QStringLiteral("staged"), diffsJson(status->staged, patches)},
                     {QStringLiteral("unstaged"), diffsJson(status->unstaged, patches)}};
    addTracking(json, info);
    return McpResult::json(json);
}

void getGitStatus(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const bool patches = arguments.value(QStringLiteral("patches")).toBool();
    inBackground([folder, patches] { return gitStatus(folder, patches); }, reply);
}

McpResult fileHistory(const QString &folder, const QString &path, int limit)
{
    const std::optional<CommitLog> log = CommitLog::read(folder, {.limit = limit, .path = path});
    if (!log)
        return McpResult::error(QStringLiteral("The project is not in a git repository"));
    QJsonArray commits;
    for (const CommitSummary &commit : log->commits) {
        QJsonObject json = commitJson(commit);
        if (commit.path != path)
            json.insert(QStringLiteral("path"), commit.path); // its name before a rename
        commits.append(json);
    }
    return McpResult::json(
        QJsonObject{{QStringLiteral("commits"), commits}, {QStringLiteral("truncated"), log->truncated}});
}

void getFileHistory(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString path =
        arguments.contains(QStringLiteral("path")) ? relativePathArgument(context, arguments) : QString();
    const int limit = std::clamp(arguments.value(QStringLiteral("limit")).toInt(30), 1, 500);
    inBackground([folder, path, limit] { return fileHistory(folder, path, limit); }, reply);
}

McpResult commitDetails(const QString &folder, const QString &id, bool patches)
{
    const CommitDetails details = CommitDetails::read(folder, id);
    QJsonObject json = commitJson(details.summary);
    json.insert(QStringLiteral("message"), details.message);
    json.insert(QStringLiteral("parents"), QJsonArray::fromStringList(details.parentIds));
    json.insert(QStringLiteral("files"), diffsJson(details.files, patches));
    return McpResult::json(json);
}

void getCommit(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString id = arguments.value(QStringLiteral("id")).toString();
    const bool patches = arguments.value(QStringLiteral("patches")).toBool();
    inBackground([folder, id, patches] { return commitDetails(folder, id, patches); }, reply);
}

McpResult snapshots(const QString &folder)
{
    QJsonArray snapshots;
    for (const Snapshot &snapshot : Snapshots::list(folder))
        snapshots.append(QJsonObject{{QStringLiteral("id"), snapshot.commitId.left(10)},
                                     {QStringLiteral("date"), snapshot.date.toString(Qt::ISODate)},
                                     {QStringLiteral("reason"), snapshot.reason},
                                     {QStringLiteral("wholeWorkingTree"), snapshot.wholeWorkingTree},
                                     {QStringLiteral("files"), QJsonArray::fromStringList(snapshot.paths)}});
    return McpResult::json(QJsonObject{{QStringLiteral("snapshots"), snapshots}});
}

void listSnapshots(const KDKToolContext &context, const QJsonObject &, const McpReply &reply)
{
    const QString folder = context.folder();
    inBackground([folder] { return snapshots(folder); }, reply);
}

std::optional<QString> readGitStatusResource(const KDKToolContext &context)
{
    try {
        const std::optional<RepositoryStatus> status = RepositoryStatus::read(context.folder());
        if (!status)
            return QStringLiteral("{}");
        QJsonObject json{{QStringLiteral("branch"), status->info.branch},
                         {QStringLiteral("head"), status->info.headId},
                         {QStringLiteral("staged"), diffsJson(status->staged, false)},
                         {QStringLiteral("unstaged"), diffsJson(status->unstaged, false)}};
        addTracking(json, status->info);
        return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
    } catch (const GitError &) {
        return std::nullopt;
    }
}

void createSnapshot(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString reason = arguments.value(QStringLiteral("reason")).toString();
    runGit(
        context,
        [folder, reason] {
            const QString id = Snapshots::createOfWorkingTree(repositoryOf(folder), reason);
            return id.isEmpty() ? QStringLiteral("Nothing to record: the files are as committed")
                                : QStringLiteral("Snapshot %1 taken").arg(id.left(10));
        },
        reply);
}

QString describeRestore(const QJsonObject &arguments)
{
    return QStringLiteral("restore the files of snapshot %1").arg(arguments.value(QStringLiteral("id")).toString());
}

QString restoreSnapshotWithId(const QString &folder, const QString &id)
{
    for (const Snapshot &snapshot : Snapshots::list(folder)) {
        if (!id.isEmpty() && snapshot.commitId.startsWith(id)) {
            const int count = Snapshots::restore(repositoryOf(folder), snapshot);
            return QStringLiteral("%1 file(s) restored").arg(count);
        }
    }
    throw GitError(QStringLiteral("No snapshot %1 (see list_snapshots)").arg(id));
}

void restoreSnapshot(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString id = arguments.value(QStringLiteral("id")).toString();
    runGit(context, [folder, id] { return restoreSnapshotWithId(folder, id); }, reply);
}

McpResult showChanges(const KDKToolContext &context, const QJsonObject &arguments)
{
    if (!context.showChanges)
        return McpResult::error(QStringLiteral("Not available"));
    const QString path = arguments.value(QStringLiteral("path")).toString();
    context.showChanges(path.isEmpty() ? QString() : relativePathArgument(context, arguments));
    return McpResult::message(QStringLiteral("Shown"));
}

McpResult setCommitMessage(const KDKToolContext &context, const QJsonObject &arguments)
{
    if (!context.setCommitMessage)
        return McpResult::error(QStringLiteral("Not available"));
    const QString message = CommitMessage::tidy(arguments.value(QStringLiteral("message")).toString());
    if (message.isEmpty())
        return McpResult::error(QStringLiteral("The message is empty"));
    context.setCommitMessage(message);
    return McpResult::message(
        QStringLiteral("The message is in Kodamiko's commit field: the user reviews it, then commits."));
}

QString describeStaging(const KDKToolContext &context, const QJsonObject &arguments)
{
    const QString action =
        arguments.value(QStringLiteral("unstage")).toBool() ? QStringLiteral("unstage") : QStringLiteral("stage");
    return QStringLiteral("%1 %2").arg(action, pathsArgument(context, arguments).join(QStringLiteral(", ")));
}

void stageFiles(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QStringList paths = pathsArgument(context, arguments);
    const bool unstage = arguments.value(QStringLiteral("unstage")).toBool();
    runGit(
        context,
        [folder, paths, unstage] {
            const RepositoryInfo repository = repositoryOf(folder);
            if (unstage)
                GitOperations::unstage(repository, paths);
            else
                GitOperations::stage(repository, paths);
            return QStringLiteral("%1 file(s) %2")
                .arg(paths.size())
                .arg(unstage ? QStringLiteral("unstaged") : QStringLiteral("staged"));
        },
        reply);
}

QString describeCommit(const QJsonObject &arguments)
{
    const QString action = arguments.value(QStringLiteral("amend")).toBool()
                               ? QStringLiteral("amend the last commit with")
                               : QStringLiteral("commit");
    return QStringLiteral("%1 \u201c%2\u201d")
        .arg(action, arguments.value(QStringLiteral("message")).toString().section(QLatin1Char('\n'), 0, 0));
}

void commit(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString message = arguments.value(QStringLiteral("message")).toString();
    const bool amend = arguments.value(QStringLiteral("amend")).toBool();
    runGit(
        context, [folder, message, amend] { return GitOperations::commit(repositoryOf(folder), message, amend); },
        reply);
}

QString describeDiscard(const KDKToolContext &context, const QJsonObject &arguments)
{
    const QString files = pathsArgument(context, arguments).join(QStringLiteral(", "));
    return arguments.value(QStringLiteral("staged")).toBool()
               ? QStringLiteral("discard every change to %1, staged ones included").arg(files)
               : QStringLiteral("discard the unstaged changes to %1").arg(files);
}

void discardChanges(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QStringList paths = pathsArgument(context, arguments);
    const bool withStaged = arguments.value(QStringLiteral("staged")).toBool();
    runGit(
        context,
        [folder, paths, withStaged] {
            const RepositoryInfo repository = repositoryOf(folder);
            if (withStaged)
                GitOperations::rollback(repository, paths);
            else
                GitOperations::discard(repository, paths);
            return QStringLiteral("%1 file(s) restored; a snapshot keeps what was discarded (list_snapshots)")
                .arg(paths.size());
        },
        reply);
}

} // namespace

void registerGitTools(McpRegistry &registry, const KDKToolContext &context)
{
    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_git_status"),
         .title = QStringLiteral("Git status"),
         .description = QStringLiteral(
             "The project's git state as Kodamiko's Diff mode shows it: branch, the remote branch it tracks and how "
             "many "
             "commits each side has that the other lacks (as of the last fetch), last commit, operation in progress, "
             "staged and unstaged changes (untracked files included), optionally with their patches."),
         .inputSchema =
             object({{QStringLiteral("patches"), boolean(QStringLiteral("Include each file's unified diff"))}})},
        handledBy(context, getGitStatus)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_file_history"),
         .title = QStringLiteral("History"),
         .description = QStringLiteral("The commits of the project, or of one file or folder, newest first, as "
                                       "Kodamiko's History mode shows them. A file is followed across its renames: "
                                       "a commit made before one gives the file's \"path\" at that time."),
         .inputSchema =
             object({{QStringLiteral("path"), string(QStringLiteral("File or folder (optional): its commits only"))},
                     {QStringLiteral("limit"), integer(QStringLiteral("Most commits (default 30)"))}})},
        handledBy(context, getFileHistory)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_commit"),
         .title = QStringLiteral("Commit"),
         .description =
             QStringLiteral("A commit's message, author, parents and changed files, optionally with their patches."),
         .inputSchema =
             object({{QStringLiteral("id"), string(QStringLiteral("Commit id (abbreviated or full)"))},
                     {QStringLiteral("patches"), boolean(QStringLiteral("Include each file's unified diff"))}},
                    {QStringLiteral("id")})},
        handledBy(context, getCommit)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("list_snapshots"),
         .title = QStringLiteral("Snapshots"),
         .description = QStringLiteral("Kodamiko's snapshots of the working directory, newest first: taken before each "
                                       "prompt and on request, they can bring files back to an earlier state "
                                       "(restore_snapshot)."),
         .inputSchema = object({})},
        handledBy(context, listSnapshots)));

    registry.addResource({
        QStringLiteral("kodamiko://git/status"),
        false,
        QStringLiteral("git-status"),
        QStringLiteral("Git status"),
        QStringLiteral("Branch and its upstream, last commit, staged and unstaged files"),
        QStringLiteral("application/json"),
        [context](const QString &) { return readGitStatusResource(context); },
    });

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("create_snapshot"),
         .title = QStringLiteral("Take a snapshot"),
         .description = QStringLiteral("Records the working directory's files in a Kodamiko snapshot (outside any "
                                       "branch), to come back to them later with restore_snapshot: before a risky "
                                       "change."),
         .inputSchema = object(
             {{QStringLiteral("reason"), string(QStringLiteral("What the snapshot is before, shown to the user"))}},
             {QStringLiteral("reason")})},
        handledBy(context, createSnapshot)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("restore_snapshot"),
         .title = QStringLiteral("Restore a snapshot"),
         .description = QStringLiteral("Brings the files of a Kodamiko snapshot back on disk (see list_snapshots), the "
                                       "current ones being saved first so that the restore can itself be undone. "
                                       "The user is asked first."),
         .inputSchema = object({{QStringLiteral("id"), string(QStringLiteral("Snapshot id (see list_snapshots)"))}},
                               {QStringLiteral("id")})},
        describeRestore, handledBy(context, restoreSnapshot)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("show_changes"),
         .title = QStringLiteral("Show changes"),
         .description =
             QStringLiteral("Shows Kodamiko's Diff mode to the user, on a file's changes if given: for them to "
                            "review what changed."),
         .inputSchema = object({{QStringLiteral("path"), string(QStringLiteral("File to show (optional)"))}})},
        handledBy(context, showChanges)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("stage_files"),
         .title = QStringLiteral("Stage files"),
         .description = QStringLiteral("Adds files' changes to git's index (or removes them from it with unstage), "
                                       "as Kodamiko's Diff mode does. The user is asked first."),
         .inputSchema =
             object({{QStringLiteral("paths"), array(string(QStringLiteral("File")), QStringLiteral("Files"))},
                     {QStringLiteral("unstage"), boolean(QStringLiteral("Remove from the index instead"))}},
                    {QStringLiteral("paths")})},
        [context](const QJsonObject &arguments) { return describeStaging(context, arguments); },
        handledBy(context, stageFiles)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("discard_changes"),
         .title = QStringLiteral("Discard changes"),
         .description = QStringLiteral(
             "Brings files back to their staged state (git restore), or with staged, to the last commit (git restore "
             "--staged --worktree): new files are deleted. A Kodamiko snapshot keeps what is discarded "
             "(restore_snapshot). The user is asked first."),
         .inputSchema =
             object({{QStringLiteral("paths"), array(string(QStringLiteral("File")), QStringLiteral("Files"))},
                     {QStringLiteral("staged"), boolean(QStringLiteral("Discard the staged changes too"))}},
                    {QStringLiteral("paths")})},
        [context](const QJsonObject &arguments) { return describeDiscard(context, arguments); },
        handledBy(context, discardChanges)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("commit"),
         .title = QStringLiteral("Commit"),
         .description = QStringLiteral("Commits what is staged with this message (amending the last commit if "
                                       "asked). The user is asked first, with the message."),
         .inputSchema = object({{QStringLiteral("message"), string(QStringLiteral("Commit message"))},
                                {QStringLiteral("amend"), boolean(QStringLiteral("Amend the last commit"))}},
                               {QStringLiteral("message")})},
        describeCommit, handledBy(context, commit)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("set_commit_message"),
         .title = QStringLiteral("Set the commit message"),
         .description =
             QStringLiteral("Puts a commit message in Kodamiko's commit field, in the Diff mode, for the user "
                            "to review and commit; nothing is committed. Use it when the user asks for "
                            "the message of the next commit."),
         .inputSchema = object({{QStringLiteral("message"), string(QStringLiteral("Commit message"))}},
                               {QStringLiteral("message")})},
        handledBy(context, setCommitMessage)));

    registerBranchTools(registry, context);
    registerIntegrationTools(registry, context);
}

} // namespace KDKTools
