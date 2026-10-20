// SPDX-License-Identifier: GPL-3.0-only

#include "AgentSessions.h"

#include "terminal/TerminalWidget.h"

#include <algorithm>

AgentSessions::AgentSessions(QObject *parent)
    : QObject(parent)
{
}

void AgentSessions::add(const Worktree &worktree, const QString &sessionId, TerminalWidget *terminal)
{
    auto activity = std::make_unique<ClaudeActivity>();
    const QPointer<TerminalWidget> tab = terminal;
    connect(activity.get(), &ClaudeActivity::stateChanged, this,
            [this, tab](ClaudeActivity::State state) { emit activityChanged(tab, state); });
    connect(activity.get(), &ClaudeActivity::attentionRequested, this,
            [this, name = worktree.name](const QString &title, const QString &message) {
                emit attentionRequested(tr("%1 — %2").arg(name, title), message);
            });
    connect(terminal, &TerminalWidget::finished, activity.get(), &ClaudeActivity::reset);
    connect(terminal, &QObject::destroyed, this, &AgentSessions::forget);
    agents_.push_back({.worktree = worktree, .sessionId = sessionId, .terminal = tab, .activity = std::move(activity)});
}

const AgentSessions::Agent *AgentSessions::find(const QWidget *terminal) const
{
    const auto agent =
        std::ranges::find_if(agents_, [terminal](const Agent &candidate) { return candidate.terminal == terminal; });
    return agent == agents_.end() ? nullptr : &*agent;
}

const AgentSessions::Agent *AgentSessions::find(const HookEvent &event) const
{
    const auto agent = std::ranges::find_if(
        agents_, [&event](const Agent &candidate) { return candidate.sessionId == event.sessionId; });
    return agent == agents_.end() ? nullptr : &*agent;
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
    Agent *agent = findBySessionId(event.sessionId);
    if (!agent)
        return;
    if (event.kind == HookEvent::Kind::PromptSubmitted)
        agent->lastPrompt = event.prompt;
    agent->activity->handle(event);
}

void AgentSessions::setWatchedTerminal(const QWidget *terminal)
{
    for (const Agent &agent : agents_)
        agent.activity->setWatched(agent.terminal == terminal);
}

AgentSessions::Agent *AgentSessions::findBySessionId(const QString &sessionId)
{
    const auto agent = std::ranges::find_if(
        agents_, [&sessionId](const Agent &candidate) { return candidate.sessionId == sessionId; });
    return agent == agents_.end() ? nullptr : &*agent;
}

const AgentSessions::Agent *AgentSessions::findByName(const QString &name) const
{
    const auto agent =
        std::ranges::find_if(agents_, [&name](const Agent &candidate) { return candidate.worktree.name == name; });
    return agent == agents_.end() ? nullptr : &*agent;
}

void AgentSessions::forget(const QObject *terminal)
{
    // A widget emits destroyed() before the QPointers to it are cleared.
    std::erase_if(agents_, [terminal](const Agent &agent) { return agent.terminal.data() == terminal; });
}
