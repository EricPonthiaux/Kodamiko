// SPDX-License-Identifier: GPL-3.0-only

#include "PermissionPrompts.h"

#include "HookServer.h"
#include "ui/UiIcons.h"

#include <algorithm>

PermissionPrompts::PermissionPrompts(Snackbar *snackbar, HookServer *hookServer, QObject *parent)
    : QObject(parent)
    , snackbar_(snackbar)
    , hookServer_(hookServer)
{
    Q_ASSERT(snackbar_ && hookServer_);
}

void PermissionPrompts::handle(const HookEvent &event, const QString &asker)
{
    switch (event.kind) {
    case HookEvent::Kind::PermissionRequested:
        ask(event, asker);
        return;
    // The user answered in the terminal, or the session went on without an
    // answer.
    case HookEvent::Kind::PromptSubmitted:
    case HookEvent::Kind::ResponseFinished:
        leaveToTerminal(event.sessionId);
        return;
    // Parallel tool calls may run while a request waits.
    case HookEvent::Kind::EditRequested:
    case HookEvent::Kind::CommandRequested:
    case HookEvent::Kind::FilesEdited:
    case HookEvent::Kind::CommandFinished:
    case HookEvent::Kind::NeedsAttention:
    case HookEvent::Kind::StatusUpdated:
        return;
    }
}

void PermissionPrompts::ask(const HookEvent &event, const QString &asker)
{
    if (!provider_)
        return;
    const quint64 key = ++lastKey_;
    std::function<void(const QJsonObject &)> respond = hookServer_->deferResponse(this, [this, key] { forget(key); });
    const Snackbar::Id message = snackbar_->post(messageFor(event, asker, key));
    pending_.append({key, event.sessionId, message, std::move(respond)});
}

SnackbarMessage PermissionPrompts::messageFor(const HookEvent &event, const QString &asker, quint64 key)
{
    const auto answer = [this, key, event](PermissionAnswer chosen) {
        return [this, key, event, chosen] { reply(key, provider_->permissionReply(event, chosen)); };
    };
    QList<SnackbarAction> actions{{tr("Deny"), answer(PermissionAnswer::Deny)}};
    if (!event.permissionSuggestions.isEmpty())
        actions.append({tr("Always Allow"), answer(PermissionAnswer::AlwaysAllow)});
    actions.append({tr("Allow"), answer(PermissionAnswer::Allow), SnackbarAction::Emphasis::Primary});
    return {
        .icon = UiIcons::icon(provider_->iconName(), provider_->brandColor()),
        .title = tr("%1 asks to use %2").arg(asker, event.message),
        .detail = event.toolDetail,
        .actions = actions,
        .closeToolTip = tr("Answer in the terminal"),
        .dismissed = [this, key] { reply(key, {}); },
    };
}

void PermissionPrompts::reply(quint64 key, const QJsonObject &output)
{
    const auto request = std::ranges::find(pending_, key, &Pending::key);
    if (request == pending_.end())
        return;
    const std::function<void(const QJsonObject &)> respond = request->respond;
    pending_.erase(request);
    respond(output);
}

void PermissionPrompts::forget(quint64 key)
{
    const auto request = std::ranges::find(pending_, key, &Pending::key);
    if (request == pending_.end())
        return;
    snackbar_->withdraw(request->message);
    pending_.erase(request);
}

void PermissionPrompts::leaveToTerminal(const QString &sessionId)
{
    const QList<Pending> requests = pending_;
    for (const Pending &request : requests) {
        if (request.sessionId != sessionId)
            continue;
        snackbar_->withdraw(request.message);
        reply(request.key, {});
    }
}
