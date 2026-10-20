// SPDX-License-Identifier: GPL-3.0-only

// The agent sessions besides the main one: each works in a worktree of its
// own (see Worktrees), in a terminal tab of its own.

#include "MainWindow.h"
#include "agent/AgentCheckPopup.h"
#include "agent/NewAgentPopup.h"
#include "editor/EditorTabs.h"
#include "git/FileChange.h"
#include "git/FileStatuses.h"
#include "git/GitCommands.h"
#include "git/RepositoryStatus.h"
#include "git/Worktrees.h"
#include "projects/ProjectStore.h"
#include "terminal/TerminalWidget.h"
#include "ui/MessagePopup.h"

#include <QFileInfo>
#include <QPointer>
#include <QTabWidget>
#include <QUuid>

#include <memory>

namespace {

// A commit message's first line, cut beyond this.
constexpr int subjectLength = 72;

// What an agent is told when it starts, besides the agent's own prompt.
// Written for the agent, not for the user: not translated.
QString agentInstructions(const Worktree &worktree, const QString &mainSession, const AgentProvider &provider)
{
    QString instructions =
        QStringLiteral(
            "You are %1, one of several %2 sessions working on this project at the same time. You work in a git "
            "worktree of your own, on the branch %3; the user's checkout is elsewhere, and its session is named %4. "
            "When a task is finished and verified, commit it with a clear message: at the end of your turn, Duet "
            "merges your new commits into the user's current branch, after replaying them on it and running the "
            "project's check. Never commit unfinished work. If Duet tells you that your commits conflict or fail "
            "the check, fix that and commit again.")
            .arg(worktree.name, provider.name(), worktree.branch, mainSession);
    if (provider.features().hasAgentMessaging)
        instructions += QStringLiteral(" To coordinate with the other sessions, use ListAgents and SendMessage.");
    return instructions;
}

// The message of the commit Duet makes of an agent's remaining changes.
QString commitMessage(const Worktree &worktree, const QString &lastPrompt)
{
    const QString subject = lastPrompt.trimmed().section(QLatin1Char('\n'), 0, 0);
    if (subject.isEmpty())
        return QObject::tr("Work of %1").arg(worktree.name);
    const QString shortened =
        subject.size() > subjectLength ? subject.left(subjectLength - 1) + QChar(0x2026) : subject;
    return QObject::tr("%1: %2").arg(worktree.name, shortened);
}

} // namespace

void MainWindow::newAgentSession()
{
    std::optional<RepositoryInfo> repository;
    QString suggestedName;
    try {
        repository = RepositoryInfo::read(folder_);
        if (repository && !repository->unborn)
            suggestedName = Worktrees::unusedName(*repository);
    } catch (const GitError &error) {
        showStatus(error.message());
        return;
    }
    if (!repository || repository->unborn) {
        const QString agent = agentProvider_->name();
        agentMessage_->popup(tr("New %1 Session in Worktree").arg(agent),
                             tr("A %1 session besides the main one works in a git worktree: open a folder of a "
                                "git repository that has at least one commit.")
                                 .arg(agent));
        return;
    }
    // The statuses also list the ignored folders, for the tree to dim them.
    const bool hasUncommittedChanges =
        std::ranges::any_of(fileStatuses_->statuses(), [](FileStatus status) { return status != FileStatus::Ignored; });
    newAgentPopup_->popup(suggestedName, hasUncommittedChanges ? NewAgentPopup::UncommittedChanges::Some
                                                               : NewAgentPopup::UncommittedChanges::None);
}

void MainWindow::createAgentWorktree(const QString &name)
{
    // Filled by the job, off the UI thread, then read on it once the job
    // succeeded: shared by the two.
    const auto worktree = std::make_shared<Worktree>();
    gitCommands_->run(
        tr("New worktree"),
        [folder = folder_, name, worktree] {
            // Read again: the folder may have changed while the popup was open.
            const std::optional<RepositoryInfo> repository = RepositoryInfo::read(folder);
            if (!repository)
                throw GitError(QObject::tr("The folder is not in a git repository"));
            *worktree = Worktrees::add(*repository, name);
            return QObject::tr("Worktree %1 created on the branch %2").arg(worktree->name, worktree->branch);
        },
        [this, worktree] { startAgentSession(*worktree); });
}

void MainWindow::startAgentSession(const Worktree &worktree)
{
    // Without Duet's MCP tools: they act on the checkout the editor shows,
    // not on the session's worktree. No ClaudeTrust call either: Claude Code
    // files a worktree's trust under its main repository, so a trusted
    // project's worktrees start without asking.
    AgentSessionOptions options;
    options.name = worktree.name;
    options.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    options.instructions = agentInstructions(worktree, QFileInfo(folder_).fileName(), *agentProvider_);
    QString error;
    const std::optional<ProcessSpec> spec =
        agentProvider_->sessionProcess(worktree.path, hookServer_.serverName(), options, &error);
    const QString agent = agentProvider_->name();
    if (!spec) {
        agentMessage_->popup(tr("%1 Session").arg(agent),
                             tr("%1\n\nThe worktree stays in %2.").arg(error, worktree.path));
        return;
    }
    TerminalWidget *terminal = addTerminal(tr("%1 · %2").arg(agent, worktree.name), *spec);
    agentSessions_.add(worktree, options.sessionId, terminal);
    updateAgentTab(terminal, AgentActivity::State::Idle);
}

void MainWindow::closeAgentSession(TerminalWidget *terminal)
{
    const AgentSessions::Agent *agent = agentSessions_.find(terminal);
    if (!agent)
        return;
    const Worktree worktree = agent->worktree;
    // Filled by the job, off the UI thread, then read on it: shared by the two.
    const auto hasWork = std::make_shared<bool>(true);
    gitCommands_->runQuietly(
        [worktree, hasWork] {
            *hasWork = Worktrees::hasWork(worktree);
            return QString();
        },
        [this, tab = QPointer<TerminalWidget>(terminal), worktree, hasWork](const QString &, const QString &error) {
            if (!error.isEmpty())
                showStatus(tr("Could not inspect the worktree %1: %2").arg(worktree.path, error));
            if (tab)
                endAgentSession(worktree, *hasWork); // an unreadable worktree counts as holding work
        });
}

void MainWindow::endAgentSession(const Worktree &worktree, bool hasWork)
{
    if (hasWork)
        closeAgentPopup_->popup(worktree); // answered in closeAgentTab()
    else
        closeAgentTab(worktree, CloseAgentPopup::WorktreeFate::Delete);
}

void MainWindow::closeAgentTab(const Worktree &worktree, CloseAgentPopup::WorktreeFate fate)
{
    // The tab may have gone while the popup was open (its session ended).
    const AgentSessions::Agent *agent = agentSessions_.findByName(worktree.name);
    const QString lastPrompt = agent ? agent->lastPrompt : QString();
    if (TerminalWidget *terminal = agent ? agent->terminal.data() : nullptr) {
        terminalTabs_->removeTab(terminalTabs_->indexOf(terminal));
        terminal->terminate();
        terminal->deleteLater();
        updateTerminalPage();
    }
    switch (fate) {
    case CloseAgentPopup::WorktreeFate::Delete:
        removeAgentWorktree(worktree);
        break;
    case CloseAgentPopup::WorktreeFate::MergeAndDelete:
        mergeAgentWork(worktree, lastPrompt, AgentMergeQueue::AfterMerge::RemoveWorktree);
        break;
    case CloseAgentPopup::WorktreeFate::Keep:
        showStatus(tr("Worktree kept in %1, on the branch %2").arg(worktree.path, worktree.branch));
        break;
    }
}

void MainWindow::removeAgentWorktree(const Worktree &worktree)
{
    gitCommands_->run(
        tr("Delete worktree"),
        [folder = folder_, worktree] {
            const std::optional<RepositoryInfo> repository = RepositoryInfo::read(folder);
            if (!repository)
                throw GitError(QObject::tr("The folder is not in a git repository"));
            Worktrees::remove(*repository, worktree);
            return QObject::tr("Worktree %1 and its branch %2 deleted").arg(worktree.name, worktree.branch);
        },
        [this] { onRepositoryChanged(); });
}

bool MainWindow::handleAgentHook(const HookEvent &event)
{
    const AgentSessions::Agent *agent = agentSessions_.find(event);
    if (!agent)
        return false;
    agentSessions_.handle(event);
    if (event.kind == HookEvent::Kind::ResponseFinished)
        queueAgentCommits(agent->worktree, agent->sentBackHead);
    return true;
}

void MainWindow::queueAgentCommits(const Worktree &worktree, const QString &sentBackHead)
{
    struct Commits {
        bool hasCommits = false;
        QString head;
    };

    // Filled by the job, off the UI thread, then read on it: shared by the two.
    const auto commits = std::make_shared<Commits>();
    gitCommands_->runQuietly(
        [worktree, commits] {
            commits->hasCommits = Worktrees::hasCommitsToMerge(worktree);
            commits->head = Worktrees::head(worktree);
            return QString();
        },
        [this, worktree, sentBackHead, commits](const QString &, const QString &error) {
            if (!error.isEmpty()) {
                showStatus(tr("Could not read %1's commits: %2").arg(worktree.name, error));
                return;
            }
            // Sent back unchanged, they would be sent back again, turn after
            // turn: the agent answered without fixing them.
            const bool isNewWork = commits->hasCommits && commits->head != sentBackHead;
            if (isNewWork)
                agentMerges_->enqueue(worktree);
        });
}

void MainWindow::mergeCurrentAgent()
{
    const AgentSessions::Agent *agent = agentSessions_.find(terminalTabs_->currentWidget());
    if (!agent) {
        showStatus(tr("Show the tab of a %1 session in a worktree to merge its work").arg(agentProvider_->name()));
        return;
    }
    mergeAgentWork(agent->worktree, agent->lastPrompt, AgentMergeQueue::AfterMerge::KeepWorktree);
}

void MainWindow::mergeAgentWork(const Worktree &worktree,
                                const QString &lastPrompt,
                                AgentMergeQueue::AfterMerge afterMerge)
{
    // Filled by the job, off the UI thread, then read on it: shared by the two.
    const auto hasCommits = std::make_shared<bool>(false);
    gitCommands_->run(
        tr("Commit %1's changes").arg(worktree.name),
        [worktree, message = commitMessage(worktree, lastPrompt), hasCommits] {
            Worktrees::commitChanges(worktree, message);
            *hasCommits = Worktrees::hasCommitsToMerge(worktree);
            return QString();
        },
        [this, worktree, afterMerge, hasCommits] {
            if (*hasCommits)
                agentMerges_->enqueue(worktree, afterMerge);
            else if (afterMerge == AgentMergeQueue::AfterMerge::RemoveWorktree)
                removeAgentWorktree(worktree);
            else
                showStatus(tr("%1 has nothing to merge").arg(worktree.name));
        });
}

void MainWindow::editAgentCheckCommand()
{
    agentCheckPopup_->popup(project_.agentCheckCommand);
}

void MainWindow::setAgentCheckCommand(const QString &command)
{
    project_.agentCheckCommand = command;
    ProjectStore::save(project_);
    agentMerges_->setCheckCommand(project_.agentCheckCommand);
}

void MainWindow::onAgentMerged(const Worktree &worktree,
                               const QString &branch,
                               const QString &head,
                               AgentMergeQueue::AfterMerge afterMerge)
{
    agentSessions_.setBaseCommit(worktree.name, head);
    onRepositoryChanged();
    for (const QString &path : editorTabs_->openFilePaths())
        editorTabs_->syncWithDisk(path);
    const QString message = tr("%1's work is merged into %2").arg(worktree.name, branch);
    showStatus(message);
    notify(tr("Agent work merged"), message);
    if (afterMerge == AgentMergeQueue::AfterMerge::KeepWorktree) {
        agentMessage_->popup(tr("Merge %1").arg(worktree.name),
                             tr("%1\n\nIts worktree stays in %2, on the branch %3: the agent can go on working there.")
                                 .arg(message, worktree.path, worktree.branch));
        return;
    }
    agentMessage_->popup(tr("Merge %1").arg(worktree.name),
                         tr("%1\n\nIts worktree and its branch %2 are deleted.").arg(message, worktree.branch));
    removeAgentWorktree(worktree);
}

void MainWindow::onAgentSentBack(const Worktree &worktree, const QString &report, const QString &head)
{
    const AgentSessions::Agent *agent = agentSessions_.findByName(worktree.name);
    const bool isRunning = agent && agent->terminal && agent->terminal->isRunning();
    if (!isRunning) {
        onAgentMergeFailed(worktree, report);
        return;
    }
    agentSessions_.sendBack(worktree.name, report, head);
    showStatus(tr("%1's commits were not merged: it is told why").arg(worktree.name));
}

void MainWindow::onAgentMergeWaiting(const Worktree &worktree, const QStringList &files)
{
    stopClosingAgents(tr("%1's work waits to be merged").arg(worktree.name));
    const QString list = files.join(QStringLiteral(", "));
    showStatus(tr("%1's work waits for your changes to %2").arg(worktree.name, list));
    notify(tr("Agent work waiting"), tr("%1's work changes files you are modifying: %2").arg(worktree.name, list));
    agentMessage_->popup(tr("Merge %1").arg(worktree.name),
                         tr("%1's work is ready, but it changes files you are modifying:\n\n%2\n\nNothing to do: "
                            "Duet merges it by itself as soon as you commit, stash or discard your changes to these "
                            "files.")
                             .arg(worktree.name, files.join(QLatin1Char('\n'))));
}

void MainWindow::onAgentMergeFailed(const Worktree &worktree, const QString &message)
{
    stopClosingAgents(tr("%1's work was not merged").arg(worktree.name));
    showStatus(tr("%1's work was not merged; its worktree stays in %2").arg(worktree.name, worktree.path));
    notify(tr("Agent work not merged"), message);
    agentMessage_->popup(tr("Merge %1").arg(worktree.name),
                         tr("%1's work was not merged into the current branch.\n\n%2\n\nIts worktree stays in %3, "
                            "on the branch %4: use %5 ▸ Merge Agent's Work once the problem is solved.")
                             .arg(worktree.name, message, worktree.path, worktree.branch, agentProvider_->name()));
}

void MainWindow::closeAgentsThenWindow()
{
    if (isClosingAgents_)
        return; // asked again while the sessions close
    isClosingAgents_ = true;
    showStatus(tr("Closing the %1 sessions in worktrees first").arg(agentProvider_->name()));
    for (TerminalWidget *terminal : agentSessions_.terminals())
        closeAgentSession(terminal);
    closeWindowOnceAgentsClosed(); // only merges left, maybe
}

void MainWindow::closeWindowOnceAgentsClosed()
{
    const bool isAgentWorkDone = agentSessions_.isEmpty() && !agentMerges_->isMerging() && !gitCommands_->isBusy();
    if (!isClosingAgents_ || !isAgentWorkDone)
        return;
    isClosingAgents_ = false;
    close();
}

void MainWindow::stopClosingAgents(const QString &reason)
{
    if (!isClosingAgents_)
        return;
    isClosingAgents_ = false;
    closingProject_ = false;
    showStatus(tr("The window stays open: %1").arg(reason));
}
