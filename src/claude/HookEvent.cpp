#include "HookEvent.h"

#include <QJsonDocument>
#include <QJsonObject>

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

} // namespace

std::optional<HookEvent> HookEvent::fromJson(const QByteArray &json)
{
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    const QString eventName = object.value(QLatin1String("hook_event_name")).toString();

    HookEvent event;
    event.sessionId = object.value(QLatin1String("session_id")).toString();

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
    if (eventName == QLatin1String("EnlumineStatusLine")) { // sent by `enlumine-hook statusline`
        event.kind = Kind::StatusUpdated;
        event.status = object.value(QLatin1String("status")).toObject();
        return event;
    }
    if (eventName == QLatin1String("PostToolUse")) {
        if (object.value(QLatin1String("tool_name")).toString() == QLatin1String("Bash")) {
            event.kind = Kind::CommandFinished;
            return event;
        }
        const QJsonObject input = object.value(QLatin1String("tool_input")).toObject();
        for (const char *key : {"file_path", "notebook_path"}) {
            const QString path = input.value(QLatin1String(key)).toString();
            if (!path.isEmpty())
                event.filePaths.append(path);
        }
        if (event.filePaths.isEmpty())
            return std::nullopt;
        event.kind = Kind::FilesEdited;
        return event;
    }
    return std::nullopt;
}
