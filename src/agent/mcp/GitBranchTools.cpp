// SPDX-License-Identifier: GPL-3.0-only

// MCP tools for branches, remotes and stashes.

#include "GitToolSupport.h"
#include "KDKTools.h"
#include "McpRegistry.h"
#include "McpWork.h"
#include "git/Branches.h"
#include "git/Remotes.h"
#include "git/Stashes.h"

#include <QJsonArray>

#include <algorithm>

namespace KDKTools {

namespace {

using namespace McpSchema;
using namespace GitToolSupport;

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

void listBranches(const KDKToolContext &context, const QJsonObject &, const McpReply &reply)
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

void listStashes(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const bool patches = arguments.value(QStringLiteral("patches")).toBool();
    inBackground([folder, patches] { return stashes(folder, patches); }, reply);
}

void fetch(const KDKToolContext &context, const QJsonObject &, const McpReply &reply)
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

void pull(const KDKToolContext &context, const QJsonObject &, const McpReply &reply)
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

QString describePush(const KDKToolContext &context, const QJsonObject &)
{
    const QString target = pushTarget(context.folder());
    return QStringLiteral("push %1").arg(target.isEmpty() ? QStringLiteral("the current branch") : target);
}

void push(const KDKToolContext &context, const QJsonObject &, const McpReply &reply)
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

void switchBranch(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString name = arguments.value(QStringLiteral("name")).toString();
    const bool create = arguments.value(QStringLiteral("create")).toBool();
    runGit(context, [folder, name, create] { return switchToBranch(folder, name, create); }, reply);
}

QString describeStash(const QJsonObject &arguments)
{
    const QString message = arguments.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? QStringLiteral("stash the uncommitted changes")
                             : QStringLiteral("stash the uncommitted changes as \u201c%1\u201d").arg(message);
}

void stashChanges(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
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

void applyStash(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
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

QString describeDeleteBranch(const QJsonObject &arguments)
{
    const QString name = arguments.value(QStringLiteral("name")).toString();
    return arguments.value(QStringLiteral("force")).toBool()
               ? QStringLiteral("delete the branch %1, even with commits no other branch has").arg(name)
               : QStringLiteral("delete the branch %1").arg(name);
}

void deleteBranch(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString name = arguments.value(QStringLiteral("name")).toString();
    const bool force = arguments.value(QStringLiteral("force")).toBool();
    runGit(
        context,
        [folder, name, force] {
            Branches::remove(repositoryOf(folder), name, force);
            return QStringLiteral("Branch %1 deleted").arg(name);
        },
        reply);
}

QString describeRenameBranch(const QJsonObject &arguments)
{
    return QStringLiteral("rename the branch %1 to %2")
        .arg(arguments.value(QStringLiteral("name")).toString(), arguments.value(QStringLiteral("newName")).toString());
}

void renameBranch(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString name = arguments.value(QStringLiteral("name")).toString();
    const QString newName = arguments.value(QStringLiteral("newName")).toString();
    runGit(
        context,
        [folder, name, newName] {
            if (const QString problem = Branches::nameError(newName); !problem.isEmpty())
                throw GitError(problem);
            Branches::rename(repositoryOf(folder), name, newName);
            return QStringLiteral("Branch %1 renamed to %2").arg(name, newName);
        },
        reply);
}

QString describeDropStash(const QJsonObject &arguments)
{
    return QStringLiteral("drop stash@{%1} without applying it").arg(arguments.value(QStringLiteral("index")).toInt());
}

void dropStash(const KDKToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const int index = arguments.value(QStringLiteral("index")).toInt();
    runGit(
        context,
        [folder, index] {
            Stashes::drop(repositoryOf(folder), index);
            return QStringLiteral("Stash dropped");
        },
        reply);
}

} // namespace

void registerBranchTools(McpRegistry &registry, const KDKToolContext &context)
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
             "Makes a branch the current one; uncommitted changes come along when they do not conflict (a Kodamiko "
             "snapshot is taken first). A remote branch (\"origin/x\") gets a local branch tracking it. With create, "
             "makes a new branch at the current commit. The user is asked first."),
         .inputSchema =
             object({{QStringLiteral("name"),
                      string(QStringLiteral("Branch: local (\"feature/x\") or remote (\"origin/x\")"))},
                     {QStringLiteral("create"), boolean(QStringLiteral("Create it at the current commit first"))}},
                    {QStringLiteral("name")})},
        describeSwitch, handledBy(context, switchBranch)));

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

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("delete_branch"),
         .title = QStringLiteral("Delete a branch"),
         .description = QStringLiteral(
             "Deletes a local branch (git branch -d). It fails when the branch has commits no other branch has, "
             "unless force is set (git branch -D), and when a worktree has it checked out. The user is asked first."),
         .inputSchema =
             object({{QStringLiteral("name"), string(QStringLiteral("Local branch"))},
                     {QStringLiteral("force"), boolean(QStringLiteral("Delete it even with unmerged commits"))}},
                    {QStringLiteral("name")})},
        describeDeleteBranch, handledBy(context, deleteBranch)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("rename_branch"),
         .title = QStringLiteral("Rename a branch"),
         .description = QStringLiteral(
             "Renames a local branch (git branch -m), the current one included. The user is asked first."),
         .inputSchema = object({{QStringLiteral("name"), string(QStringLiteral("Local branch"))},
                                {QStringLiteral("newName"), string(QStringLiteral("Its new name"))}},
                               {QStringLiteral("name"), QStringLiteral("newName")})},
        describeRenameBranch, handledBy(context, renameBranch)));

    registry.addTool(McpTool::sensitive(
        {.name = QStringLiteral("drop_stash"),
         .title = QStringLiteral("Drop a stash"),
         .description = QStringLiteral(
             "Deletes a stash without applying it (git stash drop; see list_stashes). The user is asked first."),
         .inputSchema = object({{QStringLiteral("index"), integer(QStringLiteral("Stash index (0: the most recent)"))}},
                               {QStringLiteral("index")})},
        describeDropStash, handledBy(context, dropStash)));
}

} // namespace KDKTools
