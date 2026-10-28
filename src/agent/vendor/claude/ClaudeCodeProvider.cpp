// SPDX-License-Identifier: GPL-3.0-only

#include "ClaudeCodeProvider.h"

#include "ClaudeFileRun.h"
#include "ClaudeLauncher.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace {

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
QStringList findEditedFiles(const QJsonObject &event)
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

std::optional<HookEvent> afterToolUse(HookEvent event, const QJsonObject &object)
{
    if (object.value(QLatin1String("tool_name")).toString() == QLatin1String("Bash")) {
        event.kind = HookEvent::Kind::CommandFinished;
        return event;
    }
    event.filePaths = findEditedFiles(object);
    if (event.filePaths.isEmpty())
        return std::nullopt;
    event.kind = HookEvent::Kind::FilesEdited;
    return event;
}

// PreToolUse: Claude is about to edit files or run a command; other tools
// do not concern us.
std::optional<HookEvent> beforeToolUse(HookEvent event, const QJsonObject &object)
{
    if (object.value(QLatin1String("tool_name")).toString() == QLatin1String("Bash")) {
        event.kind = HookEvent::Kind::CommandRequested;
        event.message = object.value(QLatin1String("tool_input")).toObject().value(QLatin1String("command")).toString();
        return event;
    }
    event.filePaths = findEditedFiles(object);
    if (event.filePaths.isEmpty())
        return std::nullopt;
    event.kind = HookEvent::Kind::EditRequested;
    event.message = object.value(QLatin1String("tool_name")).toString();
    return event;
}

} // namespace

QString ClaudeCodeProvider::name() const
{
    return QStringLiteral("Claude");
}

QString ClaudeCodeProvider::iconName() const
{
    return QStringLiteral("claude");
}

// Claude's own orange.
QColor ClaudeCodeProvider::brandColor() const
{
    return {0xd9, 0x77, 0x57};
}

AgentFeatures ClaudeCodeProvider::features() const
{
    return {.hasAgentMessaging = true, .hasFileRuns = true};
}

QString ClaudeCodeProvider::findExecutable() const
{
    return ClaudeLauncher::findClaudeExecutable();
}

std::optional<ProcessSpec> ClaudeCodeProvider::sessionProcess(const QString &workingDirectory,
                                                              const QString &hookServerName,
                                                              const AgentSessionOptions &options,
                                                              QString *error) const
{
    ClaudeLauncher::SessionOptions claudeOptions;
    claudeOptions.mcpReadOnlyTools = options.mcpReadOnlyTools;
    claudeOptions.name = options.name;
    claudeOptions.sessionId = options.sessionId;
    claudeOptions.pluginFolders = options.pluginFolders;
    switch (options.start) {
    case SessionStart::New:
        break;
    case SessionStart::Resume:
        claudeOptions.extraArguments << QStringLiteral("--resume") << options.conversationId;
        break;
    }
    if (!options.instructions.isEmpty())
        claudeOptions.extraArguments << QStringLiteral("--append-system-prompt") << options.instructions;
    return ClaudeLauncher::claudeProcess(workingDirectory, hookServerName, error, claudeOptions);
}

std::optional<ProcessSpec> ClaudeCodeProvider::fileRunProcess(const AgentFileRun &run, QString *error) const
{
    const QString hook = run.hookServerName.isEmpty() ? QString() : ClaudeLauncher::hookCommand();
    // The hook is what checks the file: a run without it would take an
    // unreadable file for a success.
    if (!run.hookServerName.isEmpty() && hook.isEmpty()) {
        *error =
            QCoreApplication::translate("ClaudeLauncher", "The duet-hook program is missing from the editor's folder.");
        return std::nullopt;
    }
    std::optional<ProcessSpec> spec = ClaudeLauncher::printProcess(
        run.workingDirectory,
        ClaudeFileRun::arguments(run.prompt, run.relativePath, ClaudeLauncher::pluginPath(), hook), error);
    if (spec && !hook.isEmpty())
        spec->environment.insert(QStringLiteral("DUET_SOCKET"), run.hookServerName);
    return spec;
}

QList<AgentModel> ClaudeCodeProvider::models() const
{
    // The aliases of `claude --model`: always the latest of each family.
    return {{QStringLiteral("opus"), QStringLiteral("Opus")},
            {QStringLiteral("sonnet"), QStringLiteral("Sonnet")},
            {QStringLiteral("haiku"), QStringLiteral("Haiku")}};
}

std::optional<ProcessSpec> ClaudeCodeProvider::reviewRunProcess(const AgentReviewRun &run, QString *error) const
{
    return ClaudeLauncher::printProcess(run.workingDirectory, ClaudeFileRun::reviewArguments(run), error);
}

AgentRunEvent ClaudeCodeProvider::parseRunOutput(const QByteArray &line, const QString &workingDirectory) const
{
    return ClaudeFileRun::parseLine(line, workingDirectory);
}

std::optional<HookEvent> ClaudeCodeProvider::parseHook(const QByteArray &json) const
{
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    const QString eventName = object.value(QLatin1String("hook_event_name")).toString();

    HookEvent event;
    event.sessionId = object.value(QLatin1String("session_id")).toString();
    event.workingDirectory = object.value(QLatin1String("cwd")).toString();

    if (eventName == QLatin1String("Stop") || eventName == QLatin1String("StopFailure")) {
        event.kind = HookEvent::Kind::ResponseFinished;
        event.failed = eventName == QLatin1String("StopFailure");
        if (event.failed)
            event.message = object.value(QLatin1String("error")).toString();
        return event;
    }
    if (eventName == QLatin1String("UserPromptSubmit")) {
        event.kind = HookEvent::Kind::PromptSubmitted;
        event.prompt = object.value(QLatin1String("prompt")).toString();
        return event;
    }
    if (eventName == QLatin1String("PermissionRequest")) {
        event.kind = HookEvent::Kind::PermissionRequested;
        event.message = object.value(QLatin1String("tool_name")).toString();
        return event;
    }
    if (eventName == QLatin1String("Notification")) {
        if (!waitsForUser(object.value(QLatin1String("notification_type")).toString()))
            return std::nullopt;
        event.kind = HookEvent::Kind::NeedsAttention;
        event.message = object.value(QLatin1String("message")).toString();
        return event;
    }
    if (eventName == QLatin1String("DuetStatusLine")) { // sent by `duet-hook statusline`
        event.kind = HookEvent::Kind::StatusUpdated;
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

QJsonObject ClaudeCodeProvider::promptContextReply(const QString &context) const
{
    return QJsonObject{
        {QLatin1String("hookSpecificOutput"), QJsonObject{
                                                  {QLatin1String("hookEventName"), QLatin1String("UserPromptSubmit")},
                                                  {QLatin1String("additionalContext"), context},
                                              }}};
}

QJsonObject ClaudeCodeProvider::denyToolReply(const QString &reason) const
{
    return QJsonObject{
        {QLatin1String("hookSpecificOutput"), QJsonObject{
                                                  {QLatin1String("hookEventName"), QLatin1String("PreToolUse")},
                                                  {QLatin1String("permissionDecision"), QLatin1String("deny")},
                                                  {QLatin1String("permissionDecisionReason"), reason},
                                              }}};
}

QJsonObject ClaudeCodeProvider::blockReply(const QString &reason) const
{
    return QJsonObject{{QLatin1String("decision"), QLatin1String("block")}, {QLatin1String("reason"), reason}};
}

QJsonObject ClaudeCodeProvider::editContextReply(const QString &context) const
{
    return QJsonObject{
        {QLatin1String("hookSpecificOutput"), QJsonObject{
                                                  {QLatin1String("hookEventName"), QLatin1String("PostToolUse")},
                                                  {QLatin1String("additionalContext"), context},
                                              }}};
}
