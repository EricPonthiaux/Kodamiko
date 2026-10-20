// SPDX-License-Identifier: GPL-3.0-only

#include "AgentActivity.h"

namespace {

QString failureDetail(const HookEvent &event, const QString &agentName)
{
    if (!event.failed)
        return {};
    if (event.message.isEmpty())
        return AgentActivity::tr("%1 stopped on an error").arg(agentName);
    return AgentActivity::tr("%1 stopped on an error: %2").arg(agentName, event.message);
}

} // namespace

AgentActivity::AgentActivity(QObject *parent)
    : QObject(parent)
{
}

QString AgentActivity::description() const
{
    switch (state_) {
    case State::Idle:
        return tr("%1 is waiting for a prompt").arg(agentName_);
    case State::Working:
        return tr("%1 is working").arg(agentName_);
    case State::NeedsInput:
        return detail_.isEmpty() ? tr("%1 needs your input").arg(agentName_) : detail_;
    case State::Done:
        return detail_.isEmpty() ? tr("%1 has finished").arg(agentName_) : detail_;
    }
    return {};
}

void AgentActivity::handle(const HookEvent &event)
{
    switch (event.kind) {
    case HookEvent::Kind::PromptSubmitted:
        setState(State::Working);
        break;
    case HookEvent::Kind::EditRequested:
    case HookEvent::Kind::FilesEdited:
    case HookEvent::Kind::CommandFinished:
        // A tool ran: the permission, if one was pending, was granted.
        if (state_ != State::Working)
            setState(State::Working);
        break;
    case HookEvent::Kind::PermissionRequested:
    case HookEvent::Kind::NeedsAttention:
        waitForInput(event);
        break;
    case HookEvent::Kind::ResponseFinished:
        finishResponse(event);
        break;
    case HookEvent::Kind::StatusUpdated:
        break;
    }
}

void AgentActivity::waitForInput(const HookEvent &event)
{
    const QString detail = event.kind == HookEvent::Kind::PermissionRequested
                               ? tr("Waiting for permission: %1").arg(event.message)
                               : event.message;
    const bool alreadyWaiting = state_ == State::NeedsInput;
    setState(State::NeedsInput, detail);
    // The Notification that follows a PermissionRequest says the same thing.
    if (!watched_ && !alreadyWaiting)
        emit attentionRequested(tr("%1 needs your input").arg(agentName_), detail);
}

void AgentActivity::finishResponse(const HookEvent &event)
{
    if (watched_) {
        setState(State::Idle);
        return;
    }
    const QString detail = failureDetail(event, agentName_);
    setState(State::Done, detail);
    const QString title = event.failed ? tr("%1 stopped") : tr("%1 has finished");
    emit attentionRequested(title.arg(agentName_), detail.isEmpty() ? tr("The response is ready.") : detail);
}

void AgentActivity::interrupt()
{
    if (state_ == State::Working || state_ == State::NeedsInput)
        setState(State::Idle);
}

void AgentActivity::reset()
{
    setState(State::Idle);
}

void AgentActivity::setWatched(bool watched)
{
    watched_ = watched;
    if (watched_ && state_ == State::Done)
        setState(State::Idle);
}

void AgentActivity::setState(State state, const QString &detail)
{
    const bool changed = state != state_ || detail != detail_;
    state_ = state;
    detail_ = detail;
    if (changed)
        emit stateChanged(state_);
}
