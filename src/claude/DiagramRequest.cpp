// SPDX-License-Identifier: GPL-3.0-only

#include "DiagramRequest.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace DiagramRequest {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("DiagramRequest", text);
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
    if (tool == QLatin1String("Skill"))
        return tr("Learning the diagram format");
    if (tool == QLatin1String("Write") || tool == QLatin1String("Edit") || tool == QLatin1String("MultiEdit"))
        return tr("Drawing %1").arg(path("file_path"));
    return tool;
}

} // namespace

QString prompt(const QString &kindTitle, const QString &relativePath, const QString &description)
{
    return QStringLiteral(
               "Draw a diagram in the file %2, which does not exist yet, following the duet:diagrams skill. "
               "Kind of diagram: %1.\n"
               "What the user wants it to show:\n<request>\n%3\n</request>\n"
               "When the request is about this project, read the code it refers to first and draw what the code "
               "really does, in the user's words. Write that file only. Do not ask questions: when something is "
               "unclear, choose the most likely reading. Then answer in the language of the request, in one or "
               "two sentences, with what the diagram shows.")
        .arg(kindTitle, relativePath, description.trimmed());
}

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

Event parseLine(const QByteArray &line, const QString &workDir)
{
    const QJsonObject object = QJsonDocument::fromJson(line).object();
    const QString type = object.value(QLatin1String("type")).toString();
    Event event;
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
            event.kind = Event::Kind::Steps;
    } else if (type == QLatin1String("result")) {
        event.kind = Event::Kind::Finished;
        event.failed = object.value(QLatin1String("is_error")).toBool() ||
                       object.value(QLatin1String("subtype")).toString() != QLatin1String("success");
        event.text = object.value(QLatin1String("result")).toString().trimmed();
        if (event.text.isEmpty() && event.failed)
            event.text = object.value(QLatin1String("subtype")).toString();
    }
    return event;
}

} // namespace DiagramRequest
