// SPDX-License-Identifier: GPL-3.0-only

// MCP tools that bring commits into the current branch (merge, rebase,
// cherry-pick, revert), and finish or give up the operation in progress.

#include "DuetTools.h"
#include "GitToolSupport.h"
#include "McpRegistry.h"
#include "git/Integration.h"

#include <optional>

namespace DuetTools {

namespace {

using namespace McpSchema;
using namespace GitToolSupport;

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

QString describeSkip(const QJsonObject &)
{
    return QStringLiteral("skip the commit that stopped the operation in progress");
}

void skipCommit(const DuetToolContext &context, const QJsonObject &, const McpReply &reply)
{
    const QString folder = context.folder();
    runGit(
        context,
        [folder] {
            const RepositoryInfo repository = repositoryOf(folder);
            Integration::skipCommit(repository);
            return QStringLiteral("The %1 skipped the commit and went on").arg(repository.operation);
        },
        reply);
}

// The "side" argument; nullopt when it names neither.
std::optional<Integration::ConflictSide> conflictSide(const QJsonObject &arguments)
{
    const QString side = arguments.value(QStringLiteral("side")).toString();
    if (side == QLatin1String("current"))
        return Integration::ConflictSide::Current;
    if (side == QLatin1String("incoming"))
        return Integration::ConflictSide::Incoming;
    return std::nullopt;
}

QString describeResolve(const DuetToolContext &context, const QJsonObject &arguments)
{
    const QString version = conflictSide(arguments) == Integration::ConflictSide::Current
                                ? QStringLiteral("the current branch's")
                                : QStringLiteral("the incoming");
    return QStringLiteral("resolve %1 with %2 version")
        .arg(pathsArgument(context, arguments).join(QStringLiteral(", ")), version);
}

void resolveConflicts(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const std::optional<Integration::ConflictSide> side = conflictSide(arguments);
    if (!side) {
        reply(McpResult::error(QStringLiteral("side must be \"current\" or \"incoming\"")));
        return;
    }
    const QString folder = context.folder();
    const QStringList paths = pathsArgument(context, arguments);
    runGit(
        context,
        [folder, paths, side = *side] {
            Integration::acceptVersion(repositoryOf(folder), paths, side);
            return QStringLiteral("%1 file(s) resolved and staged: continue_operation goes on").arg(paths.size());
        },
        reply);
}

QString describeCherryPick(const QJsonObject &arguments)
{
    return QStringLiteral("apply commit %1 to the current branch")
        .arg(arguments.value(QStringLiteral("id")).toString());
}

void cherryPick(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString id = arguments.value(QStringLiteral("id")).toString();
    runGit(
        context,
        [folder, id] {
            const QString output = Integration::cherryPick(repositoryOf(folder), id);
            return output.isEmpty() ? QStringLiteral("Done") : output;
        },
        reply);
}

QString describeRevert(const QJsonObject &arguments)
{
    return QStringLiteral("commit the reverse of commit %1").arg(arguments.value(QStringLiteral("id")).toString());
}

void revertCommit(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString id = arguments.value(QStringLiteral("id")).toString();
    runGit(
        context,
        [folder, id] {
            const QString output = Integration::revert(repositoryOf(folder), id);
            return output.isEmpty() ? QStringLiteral("Done") : output;
        },
        reply);
}

} // namespace

void registerIntegrationTools(McpRegistry &registry, const DuetToolContext &context)
{
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
        {.name = QStringLiteral("skip_commit"),
         .title = QStringLiteral("Skip a commit"),
         .description = QStringLiteral("Leaves out the commit that stopped the rebase, cherry-pick or revert in "
                                       "progress, and goes on with the next ones (git rebase --skip). The user is "
                                       "asked first."),
         .inputSchema = object({})},
        describeSkip, handledBy(context, skipCommit)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("resolve_conflicts"),
         .title = QStringLiteral("Resolve conflicts"),
         .description = QStringLiteral(
             "Resolves conflicted files by taking one side's version whole (git checkout --ours or --theirs, then "
             "git add): \"current\" is the branch checked out, \"incoming\" what the merge, rebase, cherry-pick or "
             "revert brings. A file that side deleted is deleted. To combine both sides, edit the file instead, then "
             "stage_files. The user is asked first."),
         .inputSchema = object(
             {{QStringLiteral("paths"), array(string(QStringLiteral("Conflicted file")), QStringLiteral("Files"))},
              {QStringLiteral("side"), string(QStringLiteral("\"current\" or \"incoming\""))}},
             {QStringLiteral("paths"), QStringLiteral("side")})},
        [context](const QJsonObject &arguments) { return describeResolve(context, arguments); },
        handledBy(context, resolveConflicts)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("cherry_pick"),
         .title = QStringLiteral("Cherry-pick"),
         .description = QStringLiteral(
             "Applies a commit's changes to the current branch as a new commit (git cherry-pick), after a Duet "
             "snapshot. On conflicts it stops with the operation in progress, as merge_branch does. The user is "
             "asked first."),
         .inputSchema = object({{QStringLiteral("id"), string(QStringLiteral("Commit id (abbreviated or full)"))}},
                               {QStringLiteral("id")})},
        describeCherryPick, handledBy(context, cherryPick)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("revert_commit"),
         .title = QStringLiteral("Revert a commit"),
         .description = QStringLiteral(
             "Commits the reverse of a commit's changes, with git's default message (git revert --no-edit), after a "
             "Duet snapshot. On conflicts it stops with the operation in progress, as merge_branch does. The user is "
             "asked first."),
         .inputSchema = object({{QStringLiteral("id"), string(QStringLiteral("Commit id (abbreviated or full)"))}},
                               {QStringLiteral("id")})},
        describeRevert, handledBy(context, revertCommit)));
}

} // namespace DuetTools
