// SPDX-License-Identifier: GPL-3.0-only

#include "HookEvent.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace {

// Notification types that mean Claude cannot go on without the user. The
// others (auth_success, idle_prompt, agent_completed…) are informative.
bool waitsForUser(const QString &notificationType)
{
    static const QStringList types{
        QStringLiteral("permission_prompt"),
        QStringLiteral("elicitation_dialog"),
        QStringLiteral("elicitation_url_dialog"),
        QStringLiteral("agent_needs_input"),
    };
    return types.contains(notificationType);
}

// The files an editing tool works on.
QStringList editedFiles(const QJsonObject &event)
{
    const QJsonObject input = event.value(QLatin1String("tool_input")).toObject();
    QStringList paths;
    for (const char *key : {"file_path", "notebook_path"}) {
        const QString path = input.value(QLatin1String(key)).toString();
        if (!path.isEmpty())
            paths.append(path);
    }
    return paths;
}

// PostToolUse: a command ran, or files were edited.
std::optional<HookEvent> afterToolUse(HookEvent event, const QJsonObject &object)
{
    if (object.value(QLatin1String("tool_name")).toString() == QLatin1String("Bash")) {
        event.kind = HookEvent::Kind::CommandFinished;
        return event;
    }
    event.filePaths = editedFiles(object);
    if (event.filePaths.isEmpty())
        return std::nullopt;
    event.kind = HookEvent::Kind::FilesEdited;
    return event;
}

// PreToolUse: Claude is about to edit files; other tools do not concern us.
std::optional<HookEvent> beforeToolUse(HookEvent event, const QJsonObject &object)
{
    event.filePaths = editedFiles(object);
    if (event.filePaths.isEmpty())
        return std::nullopt;
    event.kind = HookEvent::Kind::EditRequested;
    event.message = object.value(QLatin1String("tool_name")).toString();
    return event;
}

} // namespace

std::optional<HookEvent> HookEvent::fromJson(const QByteArray &json)
{
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    const QString eventName = object.value(QLatin1String("hook_event_name")).toString();

    HookEvent event;
    event.sessionId = object.value(QLatin1String("session_id")).toString();
    event.workingDirectory = object.value(QLatin1String("cwd")).toString();

    if (eventName == QLatin1String("Stop") || eventName == QLatin1String("StopFailure")) {
        event.kind = Kind::ResponseFinished;
        event.failed = eventName == QLatin1String("StopFailure");
        if (event.failed)
            event.message = object.value(QLatin1String("error")).toString();
        return event;
    }
    if (eventName == QLatin1String("UserPromptSubmit")) {
        event.kind = Kind::PromptSubmitted;
        event.prompt = object.value(QLatin1String("prompt")).toString();
        return event;
    }
    if (eventName == QLatin1String("PermissionRequest")) {
        event.kind = Kind::PermissionRequested;
        event.message = object.value(QLatin1String("tool_name")).toString();
        return event;
    }
    if (eventName == QLatin1String("Notification")) {
        if (!waitsForUser(object.value(QLatin1String("notification_type")).toString()))
            return std::nullopt;
        event.kind = Kind::NeedsAttention;
        event.message = object.value(QLatin1String("message")).toString();
        return event;
    }
    if (eventName == QLatin1String("DuetStatusLine")) { // sent by `duet-hook statusline`
        event.kind = Kind::StatusUpdated;
        event.status = object.value(QLatin1String("status")).toObject();
        // The session is named in the status Claude Code passed, not beside it.
        event.sessionId = event.status.value(QLatin1String("session_id")).toString();
        event.workingDirectory = event.status.value(QLatin1String("cwd")).toString();
        return event;
    }
    if (eventName == QLatin1String("PostToolUse"))
        return afterToolUse(std::move(event), object);
    if (eventName == QLatin1String("PreToolUse"))
        return beforeToolUse(std::move(event), object);
    return std::nullopt;
}
