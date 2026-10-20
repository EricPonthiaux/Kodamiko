// SPDX-License-Identifier: GPL-3.0-only

#include "ClaudeActivity.h"

namespace {

QString failureDetail(const HookEvent &event)
{
    if (!event.failed)
        return {};
    if (event.message.isEmpty())
        return ClaudeActivity::tr("Claude stopped on an error");
    return ClaudeActivity::tr("Claude stopped on an error: %1").arg(event.message);
}

} // namespace

ClaudeActivity::ClaudeActivity(QObject *parent)
    : QObject(parent)
{
}

QString ClaudeActivity::description() const
{
    switch (state_) {
    case State::Idle:
        return tr("Claude is waiting for a prompt");
    case State::Working:
        return tr("Claude is working");
    case State::NeedsInput:
        return detail_.isEmpty() ? tr("Claude needs your input") : detail_;
    case State::Done:
        return detail_.isEmpty() ? tr("Claude has finished") : detail_;
    }
    return {};
}

void ClaudeActivity::handle(const HookEvent &event)
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

void ClaudeActivity::waitForInput(const HookEvent &event)
{
    const QString detail = event.kind == HookEvent::Kind::PermissionRequested
                               ? tr("Waiting for permission: %1").arg(event.message)
                               : event.message;
    const bool alreadyWaiting = state_ == State::NeedsInput;
    setState(State::NeedsInput, detail);
    // The Notification that follows a PermissionRequest says the same thing.
    if (!watched_ && !alreadyWaiting)
        emit attentionRequested(tr("Claude needs your input"), detail);
}

void ClaudeActivity::finishResponse(const HookEvent &event)
{
    if (watched_) {
        setState(State::Idle);
        return;
    }
    const QString detail = failureDetail(event);
    setState(State::Done, detail);
    emit attentionRequested(event.failed ? tr("Claude stopped") : tr("Claude has finished"),
                            detail.isEmpty() ? tr("The response is ready.") : detail);
}

void ClaudeActivity::interrupt()
{
    if (state_ == State::Working || state_ == State::NeedsInput)
        setState(State::Idle);
}

void ClaudeActivity::reset()
{
    setState(State::Idle);
}

void ClaudeActivity::setWatched(bool watched)
{
    watched_ = watched;
    if (watched_ && state_ == State::Done)
        setState(State::Idle);
}

void ClaudeActivity::setState(State state, const QString &detail)
{
    const bool changed = state != state_ || detail != detail_;
    state_ = state;
    detail_ = detail;
    if (changed)
        emit stateChanged(state_);
}
