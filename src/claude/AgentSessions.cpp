// SPDX-License-Identifier: GPL-3.0-only

#include "AgentSessions.h"

#include "terminal/TerminalWidget.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

// Apart from the paste: an Enter read with the pasted text may be taken for
// a new line in the prompt (as in MainWindow::typeInClaudePrompt).
constexpr int submitDelayMs = 150;

// `path` with its symbolic links resolved, as Claude Code reports its
// directory; as is when it no longer exists.
QString resolvedPath(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(path) : canonical;
}

bool isWithin(const QString &path, const QString &folder)
{
    return path == folder || path.startsWith(folder + QLatin1Char('/'));
}

bool waitsForPrompt(ClaudeActivity::State state)
{
    return state == ClaudeActivity::State::Idle || state == ClaudeActivity::State::Done;
}

} // namespace

AgentSessions::AgentSessions(QObject *parent)
    : QObject(parent)
{
}

void AgentSessions::add(const Worktree &worktree, const QString &sessionId, TerminalWidget *terminal)
{
    auto activity = std::make_unique<ClaudeActivity>();
    const QPointer<TerminalWidget> tab = terminal;
    connect(activity.get(), &ClaudeActivity::stateChanged, this, [this, tab](ClaudeActivity::State state) {
        emit activityChanged(tab, state);
        deliverPendingReport(tab);
    });
    connect(activity.get(), &ClaudeActivity::attentionRequested, this,
            [this, name = worktree.name](const QString &title, const QString &message) {
                emit attentionRequested(tr("%1 — %2").arg(name, title), message);
            });
    connect(terminal, &TerminalWidget::finished, activity.get(), &ClaudeActivity::reset);
    connect(terminal, &QObject::destroyed, this, &AgentSessions::forget);
    agents_.push_back({
        .worktree = worktree,
        .sessionId = sessionId,
        .terminal = tab,
        .activity = std::move(activity),
        .directory = resolvedPath(worktree.path),
    });
}

const AgentSessions::Agent *AgentSessions::find(const QWidget *terminal) const
{
    const auto agent =
        std::ranges::find_if(agents_, [terminal](const Agent &candidate) { return candidate.terminal == terminal; });
    return agent == agents_.end() ? nullptr : &*agent;
}

const AgentSessions::Agent *AgentSessions::find(const HookEvent &event) const
{
    const std::optional<std::size_t> index = indexOf(event);
    return index ? &agents_[*index] : nullptr;
}

bool AgentSessions::owns(const HookEvent &event) const
{
    return find(event) != nullptr;
}

void AgentSessions::setBaseCommit(const QString &agentName, const QString &commit)
{
    for (Agent &agent : agents_) {
        if (agent.worktree.name == agentName)
            agent.worktree.baseCommit = commit;
    }
}

void AgentSessions::handle(const HookEvent &event)
{
    const std::optional<std::size_t> index = indexOf(event);
    if (!index)
        return;
    Agent &agent = agents_[*index];
    if (!event.sessionId.isEmpty())
        agent.sessionId = event.sessionId;
    if (event.kind == HookEvent::Kind::PromptSubmitted)
        agent.lastPrompt = event.prompt;
    agent.activity->handle(event);
}

void AgentSessions::sendBack(const QString &agentName, const QString &report, const QString &head)
{
    const auto agent = std::ranges::find_if(
        agents_, [&agentName](const Agent &candidate) { return candidate.worktree.name == agentName; });
    if (agent == agents_.end())
        return;
    agent->sentBackHead = head;
    agent->pendingReport = report;
    deliverPendingReport(agent->terminal);
}

void AgentSessions::setWatchedTerminal(const QWidget *terminal)
{
    for (const Agent &agent : agents_)
        agent.activity->setWatched(agent.terminal == terminal);
}

std::optional<std::size_t> AgentSessions::indexOf(const HookEvent &event) const
{
    const auto bySession = std::ranges::find_if(
        agents_, [&event](const Agent &candidate) { return candidate.sessionId == event.sessionId; });
    if (bySession != agents_.end())
        return bySession - agents_.begin();
    if (event.workingDirectory.isEmpty())
        return std::nullopt;
    const QString directory = resolvedPath(event.workingDirectory);
    const auto byDirectory = std::ranges::find_if(
        agents_, [&directory](const Agent &candidate) { return isWithin(directory, candidate.directory); });
    if (byDirectory == agents_.end())
        return std::nullopt;
    return byDirectory - agents_.begin();
}

const AgentSessions::Agent *AgentSessions::findByName(const QString &name) const
{
    const auto agent =
        std::ranges::find_if(agents_, [&name](const Agent &candidate) { return candidate.worktree.name == name; });
    return agent == agents_.end() ? nullptr : &*agent;
}

void AgentSessions::deliverPendingReport(const QWidget *terminal)
{
    const auto agent =
        std::ranges::find_if(agents_, [terminal](const Agent &candidate) { return candidate.terminal == terminal; });
    if (agent == agents_.end() || agent->pendingReport.isEmpty())
        return;
    const bool canType = agent->terminal && agent->terminal->isRunning() && waitsForPrompt(agent->activity->state());
    if (!canType)
        return;
    agent->terminal->paste(std::exchange(agent->pendingReport, {}));
    QTimer::singleShot(submitDelayMs, agent->terminal, &TerminalWidget::pressEnter);
}

void AgentSessions::forget(const QObject *terminal)
{
    // A widget emits destroyed() before the QPointers to it are cleared.
    std::erase_if(agents_, [terminal](const Agent &agent) { return agent.terminal.data() == terminal; });
}
