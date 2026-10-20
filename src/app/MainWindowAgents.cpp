// SPDX-License-Identifier: GPL-3.0-only

// The Claude sessions besides the main one: each works in a worktree of its
// own (see Worktrees), in a terminal tab of its own.

#include "MainWindow.h"
#include "claude/ClaudeLauncher.h"
#include "editor/EditorTabs.h"
#include "git/FileChange.h"
#include "git/FileStatuses.h"
#include "git/GitCommands.h"
#include "git/RepositoryStatus.h"
#include "git/Worktrees.h"
#include "projects/ProjectStore.h"
#include "terminal/TerminalWidget.h"

#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QUuid>

#include <memory>

namespace {

enum class WorktreeFate {
    Keep,
    Delete,
    MergeAndDelete, // its changes committed, merged, then the worktree goes
    Cancel,         // the tab stays open
};

// Apart from the paste: an Enter read with the pasted text may be taken for
// a new line in the prompt (as in MainWindow::typeInClaudePrompt).
constexpr int submitDelayMs = 150;
// A commit message's first line, cut beyond this.
constexpr int subjectLength = 72;

// What an agent is told when it starts, besides Claude Code's own prompt.
QString agentInstructions(const Worktree &worktree, const QString &mainSession)
{
    return QObject::tr(
               "You are %1, one of several Claude Code sessions working on this project at the same time. You work "
               "in a git worktree of your own, on the branch %2; the user's checkout is elsewhere, and its session "
               "is named %3. When a task is finished and verified, commit it with a clear message: at the end of "
               "your turn, Duet merges your new commits into the user's current branch, after replaying them on it "
               "and running the project's check. Never commit unfinished work. If Duet tells you that your commits "
               "conflict or fail the check, fix that and commit again. To coordinate with the other sessions, use "
               "ListAgents and SendMessage.")
        .arg(worktree.name, worktree.branch, mainSession);
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

// Types `text` in an agent's prompt and submits it.
void submitToAgent(TerminalWidget *terminal, const QString &text)
{
    terminal->paste(text);
    QTimer::singleShot(submitDelayMs, terminal, &TerminalWidget::pressEnter);
}

// What to do with the worktree of a closing session that holds work.
WorktreeFate askWorktreeFate(QWidget *parent, const Worktree &worktree)
{
    QMessageBox box(QMessageBox::Question, QObject::tr("Close %1").arg(worktree.name),
                    QObject::tr("The worktree of %1 holds changes or commits that are not merged.\n\n"
                                "Folder: %2\nBranch: %3")
                        .arg(worktree.name, worktree.path, worktree.branch),
                    QMessageBox::Cancel, parent);
    const QPushButton *merge = box.addButton(QObject::tr("Merge and Delete"), QMessageBox::AcceptRole);
    const QPushButton *keep = box.addButton(QObject::tr("Keep the Worktree"), QMessageBox::AcceptRole);
    const QPushButton *remove = box.addButton(QObject::tr("Delete the Worktree"), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == merge)
        return WorktreeFate::MergeAndDelete;
    if (box.clickedButton() == keep)
        return WorktreeFate::Keep;
    if (box.clickedButton() == remove)
        return WorktreeFate::Delete;
    return WorktreeFate::Cancel;
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
        QMessageBox::information(this, tr("New Claude Session in Worktree"),
                                 tr("A Claude session besides the main one works in a git worktree: open a folder "
                                    "of a git repository that has at least one commit."));
        return;
    }
    QString label = tr("Name of the session, of its worktree and of its branch (duet/<name>):");
    // The statuses also list the ignored folders, for the tree to dim them.
    const bool hasUncommittedChanges =
        std::ranges::any_of(fileStatuses_->statuses(), [](FileStatus status) { return status != FileStatus::Ignored; });
    if (hasUncommittedChanges)
        label += QLatin1Char('\n') + tr("The worktree starts from the last commit: your uncommitted changes will "
                                        "not be in it.");
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("New Claude Session in Worktree"), label, QLineEdit::Normal,
                                               suggestedName, &accepted)
                             .trimmed();
    if (!accepted || name.isEmpty())
        return;
    // Filled by the job, off the UI thread, then read on it once the job
    // succeeded: shared by the two.
    const auto worktree = std::make_shared<Worktree>();
    gitCommands_->run(
        tr("New worktree"),
        [repository = *repository, name, worktree] {
            *worktree = Worktrees::add(repository, name);
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
    ClaudeLauncher::SessionOptions options;
    options.name = worktree.name;
    options.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    options.extraArguments = {QStringLiteral("--append-system-prompt"),
                              agentInstructions(worktree, QFileInfo(folder_).fileName())};
    QString error;
    const std::optional<ProcessSpec> spec =
        ClaudeLauncher::claudeProcess(worktree.path, hookServer_.serverName(), &error, options);
    if (!spec) {
        QMessageBox::warning(this, tr("Claude Session"),
                             tr("%1\n\nThe worktree stays in %2.").arg(error, worktree.path));
        return;
    }
    TerminalWidget *terminal = addTerminal(tr("Claude · %1").arg(worktree.name), *spec);
    agentSessions_.add(worktree, options.sessionId, terminal);
    updateAgentTab(terminal, ClaudeActivity::State::Idle);
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
                endAgentSession(tab, worktree, *hasWork); // an unreadable worktree counts as holding work
        });
}

void MainWindow::endAgentSession(TerminalWidget *terminal, const Worktree &worktree, bool hasWork)
{
    const WorktreeFate fate = hasWork ? askWorktreeFate(this, worktree) : WorktreeFate::Delete;
    if (fate == WorktreeFate::Cancel)
        return;
    const AgentSessions::Agent *agent = agentSessions_.find(terminal);
    const QString lastPrompt = agent ? agent->lastPrompt : QString();
    terminalTabs_->removeTab(terminalTabs_->indexOf(terminal));
    terminal->terminate();
    terminal->deleteLater();
    updateTerminalPage();
    switch (fate) {
    case WorktreeFate::Delete:
        removeAgentWorktree(worktree);
        break;
    case WorktreeFate::MergeAndDelete:
        mergeAgentWork(worktree, lastPrompt, AgentMergeQueue::AfterMerge::RemoveWorktree);
        break;
    case WorktreeFate::Keep:
    case WorktreeFate::Cancel:
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
        queueAgentCommits(agent->worktree);
    return true;
}

void MainWindow::queueAgentCommits(const Worktree &worktree)
{
    // Filled by the job, off the UI thread, then read on it: shared by the two.
    const auto hasCommits = std::make_shared<bool>(false);
    gitCommands_->runQuietly(
        [worktree, hasCommits] {
            *hasCommits = Worktrees::hasCommitsToMerge(worktree);
            return QString();
        },
        [this, worktree, hasCommits](const QString &, const QString &error) {
            if (!error.isEmpty())
                showStatus(tr("Could not read %1's commits: %2").arg(worktree.name, error));
            else if (*hasCommits)
                agentMerges_->enqueue(worktree);
        });
}

void MainWindow::mergeCurrentAgent()
{
    const AgentSessions::Agent *agent = agentSessions_.find(terminalTabs_->currentWidget());
    if (!agent) {
        showStatus(tr("Show the tab of a Claude session in a worktree to merge its work"));
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
    // The dialog's label takes "&" for a shortcut marker: doubled, it shows.
    QString example = QStringLiteral("cmake -B build && cmake --build build && ctest --test-dir build");
    example.replace(QLatin1Char('&'), QStringLiteral("&&"));
    const QString label =
        tr("Command run in an agent's worktree before its commits are merged; empty: none.\nExample: %1").arg(example);
    bool accepted = false;
    const QString command = QInputDialog::getText(this, tr("Check Before Merging Agents"), label, QLineEdit::Normal,
                                                  project_.agentCheckCommand, &accepted);
    if (!accepted)
        return;
    project_.agentCheckCommand = command.trimmed();
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
    if (afterMerge == AgentMergeQueue::AfterMerge::RemoveWorktree)
        removeAgentWorktree(worktree);
}

void MainWindow::onAgentSentBack(const Worktree &worktree, const QString &report)
{
    const AgentSessions::Agent *agent = agentSessions_.findByName(worktree.name);
    const bool isRunning = agent && agent->terminal && agent->terminal->isRunning();
    if (!isRunning) {
        onAgentMergeFailed(worktree, report);
        return;
    }
    submitToAgent(agent->terminal, report);
    showStatus(tr("%1's commits were not merged: it was told why").arg(worktree.name));
}

void MainWindow::onAgentMergeFailed(const Worktree &worktree, const QString &message)
{
    showStatus(tr("%1's work was not merged; its worktree stays in %2").arg(worktree.name, worktree.path));
    notify(tr("Agent work not merged"), message);
    QMessageBox::warning(this, tr("Merge %1").arg(worktree.name),
                         tr("%1's work was not merged into the current branch.\n\n%2\n\nIts worktree stays in %3, "
                            "on the branch %4: use Claude ▸ Merge Agent's Work once the problem is solved.")
                             .arg(worktree.name, message, worktree.path, worktree.branch));
}
