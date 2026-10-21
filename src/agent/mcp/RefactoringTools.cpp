// SPDX-License-Identifier: GPL-3.0-only

// MCP tools of the refactorings: the usages by kind, Rename File and
// Change Signature. Like Duet's own commands, they show the user their
// preview when asked to (or when they reach comments and strings, or have
// conflicts), and answer once the user has chosen. Positions are 1-based.

#include "DuetTools.h"
#include "McpRegistry.h"
#include "navigation/CodeIntelligence.h"
#include "refactor/ChangeSignature.h"
#include "syntax/LanguageRegistry.h"

#include <QJsonArray>

#include <algorithm>
#include <optional>

namespace DuetTools {

namespace {

using namespace McpSchema;
using namespace LspProtocol;

QString kindName(UsageKind kind)
{
    switch (kind) {
    case UsageKind::Declaration:
        return QStringLiteral("declaration");
    case UsageKind::Read:
        return QStringLiteral("read");
    case UsageKind::Write:
        return QStringLiteral("write");
    case UsageKind::Call:
        return QStringLiteral("call");
    case UsageKind::Import:
        return QStringLiteral("import");
    case UsageKind::Textual:
        return QStringLiteral("text");
    }
    return {};
}

Position positionOf(const QJsonObject &arguments)
{
    return {std::max(0, arguments.value(QStringLiteral("line")).toInt(1) - 1),
            std::max(0, arguments.value(QStringLiteral("column")).toInt(1) - 1)};
}

Review reviewOf(const QJsonObject &arguments, Review byDefault)
{
    const QJsonValue preview = arguments.value(QStringLiteral("preview"));
    if (!preview.isBool())
        return byDefault;
    return preview.toBool() ? Review::Always : Review::WhenNeeded;
}

QJsonObject positionSchema(const QJsonObject &extra, const QStringList &optional)
{
    QJsonObject properties{
        {QStringLiteral("path"), string(QStringLiteral("File, relative to the project or absolute"))},
        {QStringLiteral("line"), integer(QStringLiteral("Line, 1-based"))},
        {QStringLiteral("column"), integer(QStringLiteral("Column, 1-based: on the symbol's name"))}};
    QStringList required{QStringLiteral("path"), QStringLiteral("line"), QStringLiteral("column")};
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        properties.insert(it.key(), it.value());
        if (!optional.contains(it.key()))
            required.append(it.key());
    }
    return object(properties, required);
}

QJsonObject previewProperty(const QString &byDefault)
{
    return boolean(QStringLiteral("Show the user the changes first, and make only what they keep (default %1). "
                                  "The answer comes once they chose.")
                       .arg(byDefault));
}

void findUsages(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
    const Position position = positionOf(arguments);
    const QString name = arguments.value(QStringLiteral("name")).toString();
    context.intelligence->collectUsages({name, path, position}, [context, reply](const UsageResults &results) {
        QJsonArray usages;
        for (const Usage &usage : results.usages) {
            usages.append(QJsonObject{{QStringLiteral("file"), context.relative(usage.path)},
                                      {QStringLiteral("line"), usage.line + 1},
                                      {QStringLiteral("column"), usage.column + 1},
                                      {QStringLiteral("kind"), kindName(usage.kind)},
                                      {QStringLiteral("text"), usage.lineText.trimmed()}});
        }
        reply(McpResult::json(
            QJsonObject{{QStringLiteral("semantic"), results.isSemantic}, {QStringLiteral("usages"), usages}}));
    });
}

void renameFile(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString from = context.absolute(arguments.value(QStringLiteral("path")).toString());
    const QString to = context.absolute(arguments.value(QStringLiteral("newPath")).toString());
    context.intelligence->renameFile(
        from, to, reviewOf(arguments, Review::WhenNeeded),
        [context, reply](const RefactoringOutcome &outcome) { reply(refactoringResult(context, outcome)); });
}

// The new parameters: {"keep": n} for the n-th of the current ones, or a
// new one's {"declaration", "value"}; nullopt (with `error`) if invalid.
std::optional<QList<ChangeSignature::Parameter>> parametersOf(const QJsonArray &array, QString &error)
{
    QList<ChangeSignature::Parameter> parameters;
    for (const QJsonValue &value : array) {
        const QJsonObject item = value.toObject();
        if (item.contains(QStringLiteral("keep"))) {
            const int kept = item.value(QStringLiteral("keep")).toInt();
            if (kept < 1) {
                error = QStringLiteral("\"keep\" counts the current parameters from 1");
                return std::nullopt;
            }
            parameters.append({.oldIndex = kept - 1});
            continue;
        }
        const QString declaration = item.value(QStringLiteral("declaration")).toString().trimmed();
        if (declaration.isEmpty()) {
            error = QStringLiteral("Each parameter needs \"keep\" or \"declaration\"");
            return std::nullopt;
        }
        parameters.append({.declaration = declaration,
                           .name = item.value(QStringLiteral("name")).toString(),
                           .valueAtCalls = item.value(QStringLiteral("value")).toString()});
    }
    return parameters;
}

void changeSignature(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    QString error;
    std::optional<QList<ChangeSignature::Parameter>> parameters =
        parametersOf(arguments.value(QStringLiteral("parameters")).toArray(), error);
    if (!parameters) {
        reply(McpResult::error(error));
        return;
    }
    const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
    const QString language = LanguageRegistry::languageForFile(path);
    for (ChangeSignature::Parameter &parameter : *parameters) {
        if (!parameter.oldIndex && parameter.name.isEmpty())
            parameter.name = ChangeSignature::nameOf(language, parameter.declaration);
    }
    context.intelligence->changeSignatureAt(
        {path, positionOf(arguments), *parameters, reviewOf(arguments, Review::Always)},
        [context, reply](const RefactoringOutcome &outcome) { reply(refactoringResult(context, outcome)); });
}

QJsonObject parameterSchema()
{
    return object({
        {QStringLiteral("keep"),
         integer(QStringLiteral("A current parameter kept, by its place in the current signature, 1-based"))},
        {QStringLiteral("declaration"),
         string(QStringLiteral("A new parameter, as declared: \"int scale\", \"scale: number = 1\", \"scale=1\""))},
        {QStringLiteral("value"),
         string(QStringLiteral("What every call passes to the new parameter; empty: the calls leave it out "
                               "(it then needs a default value)"))},
    });
}

} // namespace

McpResult refactoringResult(const DuetToolContext &context, const RefactoringOutcome &outcome)
{
    QJsonArray files;
    for (const QString &path : outcome.edit.filePaths())
        files.append(context.relative(path));
    QString status;
    switch (outcome.status) {
    case RefactoringOutcome::Status::Applied:
        status = QStringLiteral("applied");
        break;
    case RefactoringOutcome::Status::Cancelled:
        status = QStringLiteral("cancelled by the user");
        break;
    case RefactoringOutcome::Status::Failed:
        return McpResult::error(outcome.messages.isEmpty() ? QStringLiteral("The refactoring failed")
                                                           : outcome.messages.join(QLatin1Char('\n')));
    }
    QJsonObject result{{QStringLiteral("status"), status},
                       {QStringLiteral("files"), files},
                       {QStringLiteral("filesChanged"), outcome.filesChanged},
                       {QStringLiteral("edits"), outcome.edit.textEditCount()}};
    if (!outcome.messages.isEmpty())
        result.insert(QStringLiteral("conflicts"), QJsonArray::fromStringList(outcome.messages));
    return McpResult::json(result);
}

void registerRefactoringTools(McpRegistry &registry, const DuetToolContext &context)
{
    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("find_usages"),
         .title = QStringLiteral("Usages by kind"),
         .description = QStringLiteral(
             "The usages of the symbol at a position, as Duet's Find Usages has them: each one's file, line, text "
             "and kind (declaration, read, write, call, import), from the language server. Without a server, every "
             "occurrence of the word (\"semantic\": false). Prefer it to find_references to know where a symbol "
             "is changed or called."),
         .inputSchema = positionSchema({{QStringLiteral("name"), string(QStringLiteral("The symbol's name"))}},
                                       {QStringLiteral("name")})},
        handledBy(context, findUsages)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("rename_file"),
         .title = QStringLiteral("Rename or move a file"),
         .description = QStringLiteral(
             "Renames or moves a file or folder and updates what refers to it, as Duet's Move does: C and C++ "
             "includes, JavaScript and TypeScript relative imports, Python imports. One refactoring the user can "
             "undo with Undo Refactoring."),
         .inputSchema = object({{QStringLiteral("path"), string(QStringLiteral("The file or folder"))},
                                {QStringLiteral("newPath"), string(QStringLiteral("Where it goes, with its name"))},
                                {QStringLiteral("preview"), previewProperty(QStringLiteral("false"))}},
                               {QStringLiteral("path"), QStringLiteral("newPath")})},
        handledBy(context, renameFile)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("change_signature"),
         .title = QStringLiteral("Change a function's signature"),
         .description = QStringLiteral(
             "Adds, removes or reorders the parameters of the function named at a position (where it is declared), "
             "in its declarations and at every call the language server finds, as Duet's Change Signature does. "
             "\"parameters\" is the new list, in order: {\"keep\": n} for the n-th current parameter, or a new "
             "one's {\"declaration\", \"value\"}. Calls it cannot change (the function passed as a value…) are "
             "listed as conflicts."),
         .inputSchema = positionSchema(
             {{QStringLiteral("parameters"), array(parameterSchema(), QStringLiteral("The new parameters, in order"))},
              {QStringLiteral("preview"), previewProperty(QStringLiteral("true"))}},
             {QStringLiteral("preview")})},
        handledBy(context, changeSignature)));
}

} // namespace DuetTools
