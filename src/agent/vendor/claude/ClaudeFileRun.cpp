// SPDX-License-Identifier: GPL-3.0-only

#include "ClaudeFileRun.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace ClaudeFileRun {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ClaudeFileRun", text);
}

QString step(const QString &tool, const QJsonObject &input, const QString &workDir)
{
    const auto path = [&](const char *key) {
        const QString value = input.value(QLatin1String(key)).toString();
        const QString relative = QDir(workDir).relativeFilePath(value);
        return QDir::toNativeSeparators(relative.startsWith(QLatin1String("..")) ? value : relative);
    };
    if (tool == QLatin1String("Read"))
        return tr("Reading %1").arg(path("file_path"));
    if (tool == QLatin1String("Grep"))
        return tr("Searching for “%1”").arg(input.value(QLatin1String("pattern")).toString());
    if (tool == QLatin1String("Glob"))
        return tr("Looking for %1").arg(input.value(QLatin1String("pattern")).toString());
    // Claude Code's tool for an answer that follows --json-schema.
    if (tool == QLatin1String("StructuredOutput"))
        return tr("Writing the answer");
    if (tool == QLatin1String("Skill"))
        return tr("Learning the diagram format");
    if (tool == QLatin1String("Write") || tool == QLatin1String("Edit") || tool == QLatin1String("MultiEdit"))
        return tr("Drawing %1").arg(path("file_path"));
    return tool;
}

} // namespace

QStringList arguments(const QString &prompt,
                      const QString &relativePath,
                      const QString &pluginPath,
                      const QString &hookCommand)
{
    // The prompt first: --allowedTools takes every argument that follows.
    QStringList arguments{prompt, QStringLiteral("--output-format"), QStringLiteral("stream-json"),
                          QStringLiteral("--verbose"), QStringLiteral("--no-session-persistence")};
    if (!pluginPath.isEmpty())
        arguments << QStringLiteral("--plugin-dir") << QDir::toNativeSeparators(pluginPath);
    if (!hookCommand.isEmpty()) {
        const QJsonObject check{
            {QLatin1String("matcher"), QLatin1String("Write|Edit|MultiEdit")},
            {QLatin1String("hooks"), QJsonArray{QJsonObject{{QLatin1String("type"), QLatin1String("command")},
                                                            {QLatin1String("command"), hookCommand},
                                                            {QLatin1String("timeout"), 30}}}},
        };
        const QJsonObject settings{
            {QLatin1String("hooks"), QJsonObject{{QLatin1String("PostToolUse"), QJsonArray{check}}}}};
        arguments << QStringLiteral("--settings")
                  << QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact));
    }
    // Reading the project and writing the diagram: nothing else, no shell.
    arguments << QStringLiteral("--tools") << QStringLiteral("Read,Grep,Glob,Skill,Write,Edit")
              << QStringLiteral("--allowedTools") << QStringLiteral("Read") << QStringLiteral("Grep")
              << QStringLiteral("Glob") << QStringLiteral("Skill")
              << QStringLiteral("Edit(./%1)").arg(QDir::fromNativeSeparators(relativePath));
    return arguments;
}

QStringList reviewArguments(const AgentReviewRun &run)
{
    // The prompt first: --allowedTools takes every argument that follows.
    // No MCP server: the user's own could write, and Duet's would hand over
    // the editor's state, and with it the writing session's context.
    QStringList arguments{run.prompt,
                          QStringLiteral("--output-format"),
                          QStringLiteral("stream-json"),
                          QStringLiteral("--verbose"),
                          QStringLiteral("--no-session-persistence"),
                          QStringLiteral("--strict-mcp-config"),
                          QStringLiteral("--json-schema"),
                          QString::fromUtf8(QJsonDocument(run.answerSchema).toJson(QJsonDocument::Compact))};
    if (!run.model.isEmpty())
        arguments << QStringLiteral("--model") << run.model;
    arguments << QStringLiteral("--tools") << QStringLiteral("Read,Grep,Glob") << QStringLiteral("--allowedTools")
              << QStringLiteral("Read") << QStringLiteral("Grep") << QStringLiteral("Glob");
    return arguments;
}

AgentRunEvent parseLine(const QByteArray &line, const QString &workDir)
{
    const QJsonObject object = QJsonDocument::fromJson(line).object();
    const QString type = object.value(QLatin1String("type")).toString();
    AgentRunEvent event;
    if (type == QLatin1String("assistant")) {
        const QJsonArray content =
            object.value(QLatin1String("message")).toObject().value(QLatin1String("content")).toArray();
        for (const QJsonValue &value : content) {
            const QJsonObject block = value.toObject();
            if (block.value(QLatin1String("type")).toString() == QLatin1String("tool_use"))
                event.steps.append(step(block.value(QLatin1String("name")).toString(),
                                        block.value(QLatin1String("input")).toObject(), workDir));
        }
        if (!event.steps.isEmpty())
            event.kind = AgentRunEvent::Kind::Steps;
    } else if (type == QLatin1String("result")) {
        event.kind = AgentRunEvent::Kind::Finished;
        event.failed = object.value(QLatin1String("is_error")).toBool() ||
                       object.value(QLatin1String("subtype")).toString() != QLatin1String("success");
        event.text = object.value(QLatin1String("result")).toString().trimmed();
        // With --json-schema, the answer as the schema says.
        const QJsonValue structured = object.value(QLatin1String("structured_output"));
        if (structured.isObject())
            event.text = QString::fromUtf8(QJsonDocument(structured.toObject()).toJson(QJsonDocument::Compact));
        if (event.text.isEmpty() && event.failed)
            event.text = object.value(QLatin1String("subtype")).toString();
    }
    return event;
}

} // namespace ClaudeFileRun
