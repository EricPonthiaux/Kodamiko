// MCP tools for git and Duet's snapshots.

#include "DuetTools.h"
#include "McpRegistry.h"
#include "McpWork.h"
#include "claude/CommitMessage.h"
#include "git/Branches.h"
#include "git/CommitLog.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"
#include "git/Integration.h"
#include "git/Remotes.h"
#include "git/RepositoryStatus.h"
#include "git/Snapshots.h"
#include "git/Stashes.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <utility>

namespace DuetTools {

namespace {

using namespace McpSchema;

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

using GitDone = std::function<void(const QString &message, const QString &error)>;

// Tells the editor the repository changed, then replies with the job's
// message or error.
GitDone replyAfterGit(const DuetToolContext &context, McpReply reply)
{
    return [context, reply = std::move(reply)](const QString &message, const QString &error) {
        if (context.repositoryChanged)
            context.repositoryChanged();
        reply(error.isEmpty() ? McpResult::message(message) : McpResult::error(error));
    };
}

// Runs a git job with the editor's other git work (one at a time, off the
// UI thread), then tells the editor the repository changed.
void runGit(const DuetToolContext &context, GitCommands::Job job, McpReply reply)
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
void runRemoteGit(const DuetToolContext &context, GitCommands::LongJob job, McpReply reply)
{
    if (!context.git) {
        runGit(context, [job] { return job({}); }, std::move(reply));
        return;
    }
    context.git->runLongQuietly(std::move(job), replyAfterGit(context, std::move(reply)));
}

// "origin/main ↑2 ↓1" and the like, for get_git_status.
void addTracking(QJsonObject &json, const RepositoryInfo &info)
{
    json.insert(QStringLiteral("upstream"), info.upstream);
    json.insert(QStringLiteral("ahead"), info.ahead);
    json.insert(QStringLiteral("behind"), info.behind);
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

// The "path" argument, relative to the project.
QString relativePathArgument(const DuetToolContext &context, const QJsonObject &arguments)
{
    return context.relative(context.absolute(arguments.value(QStringLiteral("path")).toString()));
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

void getGitStatus(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
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

void getFileHistory(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
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

void getCommit(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
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

void listSnapshots(const DuetToolContext &context, const QJsonObject &, const McpReply &reply)
{
    const QString folder = context.folder();
    inBackground([folder] { return snapshots(folder); }, reply);
}

std::optional<QString> readGitStatusResource(const DuetToolContext &context)
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

void createSnapshot(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
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

void restoreSnapshot(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString id = arguments.value(QStringLiteral("id")).toString();
    runGit(context, [folder, id] { return restoreSnapshotWithId(folder, id); }, reply);
}

McpResult showChanges(const DuetToolContext &context, const QJsonObject &arguments)
{
    if (!context.showChanges)
        return McpResult::error(QStringLiteral("Not available"));
    const QString path = arguments.value(QStringLiteral("path")).toString();
    context.showChanges(path.isEmpty() ? QString() : relativePathArgument(context, arguments));
    return McpResult::message(QStringLiteral("Shown"));
}

McpResult setCommitMessage(const DuetToolContext &context, const QJsonObject &arguments)
{
    if (!context.setCommitMessage)
        return McpResult::error(QStringLiteral("Not available"));
    const QString message = CommitMessage::tidy(arguments.value(QStringLiteral("message")).toString());
    if (message.isEmpty())
        return McpResult::error(QStringLiteral("The message is empty"));
    context.setCommitMessage(message);
    return McpResult::message(
        QStringLiteral("The message is in Duet's commit field: the user reviews it, then commits."));
}

QString describeStaging(const DuetToolContext &context, const QJsonObject &arguments)
{
    const QString action =
        arguments.value(QStringLiteral("unstage")).toBool() ? QStringLiteral("unstage") : QStringLiteral("stage");
    return QStringLiteral("%1 %2").arg(action, pathsArgument(context, arguments).join(QStringLiteral(", ")));
}

void stageFiles(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
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

void commit(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString message = arguments.value(QStringLiteral("message")).toString();
    const bool amend = arguments.value(QStringLiteral("amend")).toBool();
    runGit(
        context, [folder, message, amend] { return GitOperations::commit(repositoryOf(folder), message, amend); },
        reply);
}

QJsonObject branchJson(const BranchInfo &branch)
{
    QJsonObject item{{QStringLiteral("name"), branch.name},
                     {QStringLiteral("remote"), branch.remote},
                     {QStringLiteral("current"), branch.current},
                     {QStringLiteral("date"), branch.date.toString(Qt::ISODate)},
                     {QStringLiteral("subject"), branch.subject}};
    if (!branch.upstream.isEmpty()) {
        item.insert(QStringLiteral("upstream"), branch.upstream);
        item.insert(QStringLiteral("upstreamGone"), branch.upstreamGone);
        item.insert(QStringLiteral("ahead"), branch.ahead);
        item.insert(QStringLiteral("behind"), branch.behind);
    }
    return item;
}

McpResult branches(const QString &folder)
{
    QJsonArray branches;
    for (const BranchInfo &branch : Branches::list(repositoryOf(folder).workDir))
        branches.append(branchJson(branch));
    return McpResult::json(QJsonObject{{QStringLiteral("branches"), branches}});
}

void listBranches(const DuetToolContext &context, const QJsonObject &, const McpReply &reply)
{
    const QString folder = context.folder();
    inBackground([folder] { return branches(folder); }, reply);
}

McpResult stashes(const QString &folder, bool patches)
{
    const QString workDir = repositoryOf(folder).workDir;
    QJsonArray stashes;
    for (const Stash &stash : Stashes::list(workDir)) {
        QJsonObject item{{QStringLiteral("index"), stash.index},
                         {QStringLiteral("message"), stash.message},
                         {QStringLiteral("date"), stash.date.toString(Qt::ISODate)}};
        if (patches)
            item.insert(QStringLiteral("patch"), truncatedPatch(Stashes::patch(workDir, stash.index)));
        stashes.append(item);
    }
    return McpResult::json(QJsonObject{{QStringLiteral("stashes"), stashes}});
}

void listStashes(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const bool patches = arguments.value(QStringLiteral("patches")).toBool();
    inBackground([folder, patches] { return stashes(folder, patches); }, reply);
}

void fetch(const DuetToolContext &context, const QJsonObject &, const McpReply &reply)
{
    const QString folder = context.folder();
    runRemoteGit(
        context, [folder](const Git::Feedback &feedback) { return Remotes::fetch(repositoryOf(folder), feedback); },
        reply);
}

QString describePull(const QJsonObject &)
{
    return QStringLiteral("pull the upstream's commits into the current branch");
}

void pull(const DuetToolContext &context, const QJsonObject &, const McpReply &reply)
{
    const QString folder = context.folder();
    runRemoteGit(
        context, [folder](const Git::Feedback &feedback) { return Remotes::pull(repositoryOf(folder), feedback); },
        reply);
}

// "main to origin/main": what a push sends where; empty when unknown.
QString pushTarget(const QString &folder)
{
    try {
        const std::optional<RepositoryInfo> info = RepositoryInfo::read(folder);
        if (!info)
            return {};
        return info->upstream.isEmpty() ? info->branch : info->branch + QStringLiteral(" to ") + info->upstream;
    } catch (const GitError &) {
        return {}; // the confirmation names no branch then
    }
}

QString describePush(const DuetToolContext &context, const QJsonObject &)
{
    const QString target = pushTarget(context.folder());
    return QStringLiteral("push %1").arg(target.isEmpty() ? QStringLiteral("the current branch") : target);
}

void push(const DuetToolContext &context, const QJsonObject &, const McpReply &reply)
{
    const QString folder = context.folder();
    runRemoteGit(
        context, [folder](const Git::Feedback &feedback) { return Remotes::push(repositoryOf(folder), feedback); },
        reply);
}

QString describeSwitch(const QJsonObject &arguments)
{
    const QString action = arguments.value(QStringLiteral("create")).toBool() ? QStringLiteral("create and switch to")
                                                                              : QStringLiteral("switch to");
    return QStringLiteral("%1 %2").arg(action, arguments.value(QStringLiteral("name")).toString());
}

// A name stands for a remote branch only when no local branch has it.
bool namesRemoteBranchOnly(const QString &workDir, const QString &name)
{
    const QList<BranchInfo> branches = Branches::list(workDir);
    const bool hasLocal = std::ranges::any_of(
        branches, [&name](const BranchInfo &branch) { return branch.name == name && !branch.remote; });
    const bool hasRemote = std::ranges::any_of(
        branches, [&name](const BranchInfo &branch) { return branch.name == name && branch.remote; });
    return hasRemote && !hasLocal;
}

QString switchToBranch(const QString &folder, const QString &name, bool create)
{
    const RepositoryInfo repository = repositoryOf(folder);
    if (create) {
        if (const QString problem = Branches::nameError(name); !problem.isEmpty())
            throw GitError(problem);
        Branches::create(repository, name, true);
        return QStringLiteral("Switched to the new branch %1").arg(name);
    }
    const bool remote = namesRemoteBranchOnly(repository.workDir, name);
    Branches::switchTo(repository, name, remote);
    return QStringLiteral("Switched to %1").arg(remote ? Branches::nameOnRemote(name) : name);
}

void switchBranch(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString name = arguments.value(QStringLiteral("name")).toString();
    const bool create = arguments.value(QStringLiteral("create")).toBool();
    runGit(context, [folder, name, create] { return switchToBranch(folder, name, create); }, reply);
}

QString describeMerge(const QJsonObject &arguments)
{
    const QString action = arguments.value(QStringLiteral("rebase")).toBool()
                               ? QStringLiteral("rebase the current branch onto")
                               : QStringLiteral("merge into the current branch");
    return QStringLiteral("%1 %2").arg(action, arguments.value(QStringLiteral("branch")).toString());
}

void mergeBranch(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString branch = arguments.value(QStringLiteral("branch")).toString();
    const bool rebase = arguments.value(QStringLiteral("rebase")).toBool();
    runGit(
        context,
        [folder, branch, rebase] {
            const RepositoryInfo repository = repositoryOf(folder);
            const QString output =
                rebase ? Integration::rebase(repository, branch) : Integration::merge(repository, branch);
            return output.isEmpty() ? QStringLiteral("Done") : output;
        },
        reply);
}

QString describeContinue(const QJsonObject &)
{
    return QStringLiteral("continue the operation in progress");
}

void continueOperation(const DuetToolContext &context, const QJsonObject &, const McpReply &reply)
{
    const QString folder = context.folder();
    runGit(
        context,
        [folder] {
            const RepositoryInfo repository = repositoryOf(folder);
            Integration::continueOperation(repository);
            return QStringLiteral("The %1 went on").arg(repository.operation);
        },
        reply);
}

QString describeAbort(const QJsonObject &)
{
    return QStringLiteral("abort the operation in progress");
}

void abortOperation(const DuetToolContext &context, const QJsonObject &, const McpReply &reply)
{
    const QString folder = context.folder();
    runGit(
        context,
        [folder] {
            const RepositoryInfo repository = repositoryOf(folder);
            Integration::abortOperation(repository);
            return QStringLiteral("The %1 was aborted").arg(repository.operation);
        },
        reply);
}

QString describeStash(const QJsonObject &arguments)
{
    const QString message = arguments.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? QStringLiteral("stash the uncommitted changes")
                             : QStringLiteral("stash the uncommitted changes as \u201c%1\u201d").arg(message);
}

void stashChanges(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString message = arguments.value(QStringLiteral("message")).toString();
    const bool includeUntracked = !arguments.value(QStringLiteral("keepNewFiles")).toBool();
    runGit(
        context,
        [folder, message, includeUntracked] {
            Stashes::push(repositoryOf(folder), message, includeUntracked);
            return QStringLiteral("Changes stashed");
        },
        reply);
}

QString describeApplyStash(const QJsonObject &arguments)
{
    return QStringLiteral("%1 stash@{%2}")
        .arg(arguments.value(QStringLiteral("keep")).toBool() ? QStringLiteral("apply") : QStringLiteral("pop"))
        .arg(arguments.value(QStringLiteral("index")).toInt());
}

void applyStash(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const int index = arguments.value(QStringLiteral("index")).toInt();
    const bool keep = arguments.value(QStringLiteral("keep")).toBool();
    runGit(
        context,
        [folder, index, keep] {
            const RepositoryInfo repository = repositoryOf(folder);
            if (keep)
                Stashes::apply(repository, index);
            else
                Stashes::pop(repository, index);
            return keep ? QStringLiteral("Stash applied") : QStringLiteral("Stash applied and dropped");
        },
        reply);
}

} // namespace

void registerGitTools(McpRegistry &registry, const DuetToolContext &context)
{
    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_git_status"),
         .title = QStringLiteral("Git status"),
         .description = QStringLiteral(
             "The project's git state as Duet's Diff mode shows it: branch, the remote branch it tracks and how many "
             "commits each side has that the other lacks (as of the last fetch), last commit, operation in progress, "
             "staged and unstaged changes (untracked files included), optionally with their patches."),
         .inputSchema =
             object({{QStringLiteral("patches"), boolean(QStringLiteral("Include each file's unified diff"))}})},
        handledBy(context, getGitStatus)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_file_history"),
         .title = QStringLiteral("History"),
         .description = QStringLiteral("The commits of the project, or of one file or folder, newest first, as "
                                       "Duet's History mode shows them. A file is followed across its renames: "
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
         .description = QStringLiteral("Duet's snapshots of the working directory, newest first: taken before each "
                                       "prompt and on request, they can bring files back to an earlier state "
                                       "(restore_snapshot)."),
         .inputSchema = object({})},
        handledBy(context, listSnapshots)));

    registry.addResource({
        QStringLiteral("duet://git/status"),
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
         .description = QStringLiteral("Records the working directory's files in a Duet snapshot (outside any "
                                       "branch), to come back to them later with restore_snapshot: before a risky "
                                       "change."),
         .inputSchema = object(
             {{QStringLiteral("reason"), string(QStringLiteral("What the snapshot is before, shown to the user"))}},
             {QStringLiteral("reason")})},
        handledBy(context, createSnapshot)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("restore_snapshot"),
         .title = QStringLiteral("Restore a snapshot"),
         .description = QStringLiteral("Brings the files of a Duet snapshot back on disk (see list_snapshots), the "
                                       "current ones being saved first so that the restore can itself be undone. "
                                       "The user is asked first."),
         .inputSchema = object({{QStringLiteral("id"), string(QStringLiteral("Snapshot id (see list_snapshots)"))}},
                               {QStringLiteral("id")})},
        describeRestore, handledBy(context, restoreSnapshot)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("show_changes"),
         .title = QStringLiteral("Show changes"),
         .description = QStringLiteral("Shows Duet's Diff mode to the user, on a file's changes if given: for them to "
                                       "review what changed."),
         .inputSchema = object({{QStringLiteral("path"), string(QStringLiteral("File to show (optional)"))}})},
        handledBy(context, showChanges)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("stage_files"),
         .title = QStringLiteral("Stage files"),
         .description = QStringLiteral("Adds files' changes to git's index (or removes them from it with unstage), "
                                       "as Duet's Diff mode does. The user is asked first."),
         .inputSchema =
             object({{QStringLiteral("paths"), array(string(QStringLiteral("File")), QStringLiteral("Files"))},
                     {QStringLiteral("unstage"), boolean(QStringLiteral("Remove from the index instead"))}},
                    {QStringLiteral("paths")})},
        [context](const QJsonObject &arguments) { return describeStaging(context, arguments); },
        handledBy(context, stageFiles)));

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
         .description = QStringLiteral("Puts a commit message in Duet's commit field, in the Diff mode, for the user "
                                       "to review and commit; nothing is committed. Use it when the user asks for "
                                       "the message of the next commit."),
         .inputSchema = object({{QStringLiteral("message"), string(QStringLiteral("Commit message"))}},
                               {QStringLiteral("message")})},
        handledBy(context, setCommitMessage)));

    registerBranchTools(registry, context);
}

void registerBranchTools(McpRegistry &registry, const DuetToolContext &context)
{
    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("list_branches"),
         .title = QStringLiteral("Branches"),
         .description = QStringLiteral(
             "The local branches, then the remote ones as of the last fetch: for each, whether it is the current one, "
             "the remote branch it tracks, how many commits it has that its upstream lacks (ahead) and the reverse "
             "(behind), and its last commit."),
         .inputSchema = object({})},
        handledBy(context, listBranches)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("list_stashes"),
         .title = QStringLiteral("Stashes"),
         .description = QStringLiteral(
             "The changes put aside with git stash, newest first (index 0), optionally with their patches."),
         .inputSchema =
             object({{QStringLiteral("patches"), boolean(QStringLiteral("Include each stash's unified diff"))}})},
        handledBy(context, listStashes)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("fetch"),
         .title = QStringLiteral("Fetch"),
         .description = QStringLiteral("Updates the remote branches from every remote (git fetch --all --prune), "
                                       "without touching the local branches or the files: then get_git_status tells "
                                       "how far behind the branch is."),
         .inputSchema = object({})},
        handledBy(context, fetch)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("pull"),
         .title = QStringLiteral("Pull"),
         .description = QStringLiteral(
             "Brings the upstream's new commits into the current branch, following the user's pull.rebase and "
             "pull.ff settings; without either, only a fast-forward (it fails when both sides have new commits: then "
             "merge_branch or rebase). The user is asked first."),
         .inputSchema = object({})},
        describePull, handledBy(context, pull)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("push"),
         .title = QStringLiteral("Push"),
         .description = QStringLiteral(
             "Sends the current branch's commits to its upstream; a branch without one is published to \"origin\" "
             "(or the only remote) under the same name. Never forces: a push the remote rejects needs a pull first. "
             "The user is asked first."),
         .inputSchema = object({})},
        [context](const QJsonObject &arguments) { return describePush(context, arguments); },
        handledBy(context, push)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("switch_branch"),
         .title = QStringLiteral("Switch branch"),
         .description = QStringLiteral(
             "Makes a branch the current one; uncommitted changes come along when they do not conflict (a Duet "
             "snapshot is taken first). A remote branch (\"origin/x\") gets a local branch tracking it. With create, "
             "makes a new branch at the current commit. The user is asked first."),
         .inputSchema =
             object({{QStringLiteral("name"),
                      string(QStringLiteral("Branch: local (\"feature/x\") or remote (\"origin/x\")"))},
                     {QStringLiteral("create"), boolean(QStringLiteral("Create it at the current commit first"))}},
                    {QStringLiteral("name")})},
        describeSwitch, handledBy(context, switchBranch)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("merge_branch"),
         .title = QStringLiteral("Merge or rebase"),
         .description = QStringLiteral(
             "Brings another branch's commits into the current branch: a merge (git's default message), or with "
             "rebase, replays the current branch's own commits on top of it. On conflicts it stops with the "
             "operation in progress (get_git_status): resolve the files, stage them, then continue_operation, or "
             "abort_operation. The user is asked first."),
         .inputSchema = object(
             {{QStringLiteral("branch"), string(QStringLiteral("Branch to bring in (\"main\", \"origin/main\")"))},
              {QStringLiteral("rebase"), boolean(QStringLiteral("Rebase onto it instead of merging"))}},
             {QStringLiteral("branch")})},
        describeMerge, handledBy(context, mergeBranch)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("continue_operation"),
         .title = QStringLiteral("Continue merge or rebase"),
         .description = QStringLiteral("Goes on with the merge, rebase, cherry-pick or revert in progress once its "
                                       "conflicts are resolved and staged, committing the result. The user is asked "
                                       "first."),
         .inputSchema = object({})},
        describeContinue, handledBy(context, continueOperation)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("abort_operation"),
         .title = QStringLiteral("Abort merge or rebase"),
         .description = QStringLiteral("Gives up the merge, rebase, cherry-pick or revert in progress: the branch and "
                                       "the files go back to how they were before it. The user is asked first."),
         .inputSchema = object({})},
        describeAbort, handledBy(context, abortOperation)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("stash_changes"),
         .title = QStringLiteral("Stash changes"),
         .description = QStringLiteral(
             "Puts the uncommitted changes aside (git stash), new files included unless asked otherwise: the files go "
             "back to the last commit. apply_stash brings them back. The user is asked first."),
         .inputSchema =
             object({{QStringLiteral("message"), string(QStringLiteral("What the changes are, shown in the list"))},
                     {QStringLiteral("keepNewFiles"), boolean(QStringLiteral("Leave untracked files in place"))}})},
        describeStash, handledBy(context, stashChanges)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("apply_stash"),
         .title = QStringLiteral("Apply a stash"),
         .description = QStringLiteral("Brings a stash's changes back (see list_stashes), and drops it unless keep is "
                                       "set. The user is asked first."),
         .inputSchema =
             object({{QStringLiteral("index"), integer(QStringLiteral("Stash index (0: the most recent)"))},
                     {QStringLiteral("keep"), boolean(QStringLiteral("Keep the stash after applying it"))}})},
        describeApplyStash, handledBy(context, applyStash)));
}

} // namespace DuetTools
