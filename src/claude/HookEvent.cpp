#include "HookEvent.h"

#include <QJsonDocument>
#include <QJsonObject>

std::optional<HookEvent> HookEvent::fromJson(const QByteArray &json)
{
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    const QString eventName = object.value(QLatin1String("hook_event_name")).toString();

    HookEvent event;
    event.sessionId = object.value(QLatin1String("session_id")).toString();

    if (eventName == QLatin1String("Stop")) {
        event.kind = Kind::ResponseFinished;
        return event;
    }
    if (eventName == QLatin1String("PostToolUse")) {
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
