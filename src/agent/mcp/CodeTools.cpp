// SPDX-License-Identifier: GPL-3.0-only

// MCP tools backed by the language servers Duet runs: problems,
// definitions, usages, documentation, rename, quick fixes, formatting.
// Positions are 1-based lines and columns; the files' unsaved text is what
// the servers see.

#include "DuetTools.h"
#include "McpRegistry.h"
#include "McpWork.h"
#include "diagnostics/ProblemStore.h"
#include "editor/CodeFormatter.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "editor/TextFile.h"
#include "lsp/LanguageServers.h"
#include "lsp/LspProtocol.h"
#include "navigation/CodeIntelligence.h"
#include "syntax/LanguageRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

namespace DuetTools {

namespace {

using namespace McpSchema;
using namespace LspProtocol;

// How long a server may take to report problems after a change.
constexpr int diagnosticsWaitMs = 4000;

QString textOf(const DuetToolContext &context, const QString &path)
{
    const QHash<QString, QString> buffers = context.tabs->unsavedTexts();
    if (buffers.contains(path))
        return buffers.value(path);
    QString error;
    const std::optional<DecodedText> decoded = TextFile::load(path, &error);
    return decoded ? decoded->text : QString();
}

QString severityName(int severity)
{
    switch (severity) {
    case 1:
        return QStringLiteral("error");
    case 2:
        return QStringLiteral("warning");
    case 3:
        return QStringLiteral("information");
    default:
        return QStringLiteral("hint");
    }
}

// The server of `path`'s language, with the file's current text (and that
// of the other open files of its languages) sent to it; null, with `error`,
// when there is none.
LspClient *serverFor(const DuetToolContext &context, const QString &path, QString *error, bool *sent = nullptr)
{
    if (!context.languageServers) {
        *error = QStringLiteral("Language servers are off in Duet");
        return nullptr;
    }
    const QString languageId = LanguageRegistry::languageForFile(path);
    LspClient *client = context.languageServers->clientFor(languageId);
    if (!client) {
        const QString failure = context.languageServers->failure(languageId);
        *error =
            failure.isEmpty() ? QStringLiteral("No language server for %1 files in Duet").arg(languageId) : failure;
        return nullptr;
    }
    const bool changed =
        context.languageServers->syncDocument(client, path, languageId, textOf(context, path).toUtf8());
    if (sent)
        *sent = changed;
    if (const std::optional<LanguageServerSpec> spec = context.languageServers->specFor(languageId)) {
        for (const QString &open : context.tabs->openFilePaths()) {
            const EditorWidget *editor = context.tabs->editorFor(open);
            if (editor && open != path && spec->languages.contains(editor->languageId()))
                context.languageServers->syncDocument(client, open, editor->languageId(),
                                                      editor->utf8Bytes().toByteArray());
        }
    }
    return client;
}

QJsonObject positionParams(const QJsonObject &arguments, const QString &path)
{
    return QJsonObject{
        {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uriOf(path)}}},
        {QStringLiteral("position"),
         QJsonObject{
             {QStringLiteral("line"), std::max(0, arguments.value(QStringLiteral("line")).toInt(1) - 1)},
             {QStringLiteral("character"), std::max(0, arguments.value(QStringLiteral("column")).toInt(1) - 1)}}}};
}

// A position's arguments, with `extra` ones (required unless listed in
// `optional`).
QJsonObject positionSchema(const QJsonObject &extra = {}, const QStringList &optional = {})
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

// Locations with the text of their line, for Claude to read.
QJsonArray locationsJson(const DuetToolContext &context, const QList<Location> &locations)
{
    QHash<QString, QStringList> lines;
    QJsonArray array;
    for (const Location &location : locations) {
        if (!lines.contains(location.filePath))
            lines.insert(location.filePath, textOf(context, location.filePath).split(QLatin1Char('\n')));
        const QStringList &text = lines[location.filePath];
        array.append(QJsonObject{{QStringLiteral("file"), context.relative(location.filePath)},
                                 {QStringLiteral("line"), location.range.start.line + 1},
                                 {QStringLiteral("column"), location.range.start.character + 1},
                                 {QStringLiteral("text"), text.value(location.range.start.line).trimmed()}});
    }
    return array;
}

QJsonArray diagnosticsJson(const QJsonArray &diagnostics)
{
    QJsonArray array;
    for (const QJsonValue &value : diagnostics) {
        const QJsonObject diagnostic = value.toObject();
        const Range range = rangeFrom(diagnostic.value(QStringLiteral("range")));
        QJsonObject item{
            {QStringLiteral("line"), range.start.line + 1},
            {QStringLiteral("column"), range.start.character + 1},
            {QStringLiteral("endLine"), range.end.line + 1},
            {QStringLiteral("severity"), severityName(diagnostic.value(QStringLiteral("severity")).toInt(1))},
            {QStringLiteral("message"), diagnostic.value(QStringLiteral("message")).toString()}};
        if (diagnostic.contains(QStringLiteral("source")))
            item.insert(QStringLiteral("source"), diagnostic.value(QStringLiteral("source")).toString());
        if (diagnostic.contains(QStringLiteral("code")))
            item.insert(QStringLiteral("code"), diagnostic.value(QStringLiteral("code")).toVariant().toString());
        array.append(item);
    }
    return array;
}

// The problems the last builds found in `path` that no server reports.
QJsonArray buildProblemsJson(const DuetToolContext &context, const QString &path)
{
    QJsonArray array;
    if (!context.problems)
        return array;
    // The servers' first, then the builds'.
    const Problems problems = context.problems->problems(path).mid(context.problems->serverProblems(path).size());
    for (const Problem &problem : problems) {
        QJsonObject item{{QStringLiteral("line"), problem.line + 1},
                         {QStringLiteral("severity"), problem.severity == Diagnostic::Severity::Error
                                                          ? QStringLiteral("error")
                                                          : QStringLiteral("warning")},
                         {QStringLiteral("message"), problem.message},
                         {QStringLiteral("source"), QStringLiteral("build: %1").arg(problem.source)}};
        if (problem.column >= 0)
            item.insert(QStringLiteral("column"), problem.column + 1);
        array.append(item);
    }
    return array;
}

// Problems an editor shows that do not come from a server (Claude's
// configuration files).
QJsonArray editorProblems(const EditorWidget *editor)
{
    QJsonArray array;
    for (const Diagnostic &diagnostic : editor->diagnostics()) {
        int line = 0;
        int index = 0;
        editor->lineIndexFromPosition(diagnostic.start, &line, &index);
        array.append(QJsonObject{{QStringLiteral("line"), line + 1},
                                 {QStringLiteral("column"), index + 1},
                                 {QStringLiteral("severity"), diagnostic.severity == Diagnostic::Severity::Error
                                                                  ? QStringLiteral("error")
                                                                  : QStringLiteral("warning")},
                                 {QStringLiteral("message"), diagnostic.message}});
    }
    return array;
}

// The problems of one file: the servers', else the editor's own, then the
// builds'.
QJsonArray fileProblems(const DuetToolContext &context, const QString &path)
{
    QJsonArray problems = diagnosticsJson(context.languageServers->diagnostics(path));
    const QJsonArray built = buildProblemsJson(context, path);
    if (problems.isEmpty() && built.isEmpty()) {
        if (const EditorWidget *editor = context.tabs->editorFor(path))
            problems = editorProblems(editor);
    }
    for (const QJsonValue &problem : built)
        problems.append(problem);
    return problems;
}

int errorCount(const QJsonArray &problems)
{
    return int(std::ranges::count_if(problems, [](const QJsonValue &problem) {
        return problem.toObject().value(QStringLiteral("severity")).toString() == QLatin1String("error");
    }));
}

// Collects the problems of several files, waiting for the servers to
// report those of the files they were just sent.
class DiagnosticsCollector : public QObject {
public:
    DiagnosticsCollector(const DuetToolContext &context, const QStringList &paths, McpReply reply)
        : context_(context)
        , reply_(std::move(reply))
    {
        for (const QString &path : paths) {
            QString error;
            bool sent = false;
            if (serverFor(context, path, &error, &sent) &&
                (sent || context.languageServers->diagnostics(path).isEmpty()))
                waiting_.insert(path);
            paths_.append(path);
        }
        connect(context.languageServers, &LanguageServers::diagnosticsChanged, this, [this](const QString &path) {
            waiting_.remove(path);
            if (waiting_.isEmpty())
                finish();
        });
        QTimer::singleShot(waiting_.isEmpty() ? 0 : diagnosticsWaitMs, this, &DiagnosticsCollector::finish);
    }

private:
    void finish()
    {
        if (done_)
            return;
        done_ = true;
        QJsonArray files;
        int errors = 0;
        for (const QString &path : paths_) {
            const QJsonArray problems = fileProblems(context_, path);
            errors += errorCount(problems);
            if (!problems.isEmpty())
                files.append(QJsonObject{{QStringLiteral("file"), context_.relative(path)},
                                         {QStringLiteral("problems"), problems}});
        }
        QJsonObject result{{QStringLiteral("files"), files}, {QStringLiteral("errors"), errors}};
        if (!waiting_.isEmpty())
            result.insert(QStringLiteral("notYetChecked"), QJsonArray::fromStringList(pendingPaths()));
        reply_(McpResult::json(result));
        deleteLater();
    }

    QStringList pendingPaths() const
    {
        QStringList pending;
        for (const QString &path : waiting_)
            pending.append(context_.relative(path));
        return pending;
    }

    DuetToolContext context_;
    McpReply reply_;
    QStringList paths_;
    QSet<QString> waiting_;
    bool done_ = false;
};

// A request at a position: the LSP method, its parameters besides the
// position, and how its result becomes the tool's.
struct PositionalRequest {
    QString method;
    QJsonObject extraParams;
    std::function<McpResult(const QJsonValue &)> convert;
};

// Sends a positional request to the file's server and replies with
// `convert`'s result.
void sendPositionalRequest(const DuetToolContext &context,
                           const QJsonObject &arguments,
                           PositionalRequest request,
                           const McpReply &reply)
{
    const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
    QString error;
    LspClient *client = serverFor(context, path, &error);
    if (!client) {
        reply(McpResult::error(error));
        return;
    }
    QJsonObject params = positionParams(arguments, path);
    for (auto it = request.extraParams.begin(); it != request.extraParams.end(); ++it)
        params.insert(it.key(), it.value());
    client->request(request.method, params,
                    [reply, convert = std::move(request.convert)](const QJsonValue &result, const QString &failure) {
                        reply(failure.isEmpty() ? convert(result) : McpResult::error(failure));
                    });
}

// The code actions at a position, for a list or to apply one.
void requestCodeActions(const DuetToolContext &context,
                        const QJsonObject &arguments,
                        const std::function<void(LspClient *, QList<QJsonObject>, const QString &)> &done)
{
    const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
    QString error;
    LspClient *client = serverFor(context, path, &error);
    if (!client) {
        done(nullptr, {}, error);
        return;
    }
    const QJsonObject position = positionParams(arguments, path).value(QStringLiteral("position")).toObject();
    const int line = position.value(QStringLiteral("line")).toInt();
    // The problems on that line are the context of the quick fixes.
    QJsonArray onLine;
    for (const QJsonValue &diagnostic : context.languageServers->diagnostics(path)) {
        const Range range = rangeFrom(diagnostic.toObject().value(QStringLiteral("range")));
        if (range.start.line <= line && range.end.line >= line)
            onLine.append(diagnostic);
    }
    const QJsonObject params{
        {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uriOf(path)}}},
        {QStringLiteral("range"), QJsonObject{{QStringLiteral("start"), position}, {QStringLiteral("end"), position}}},
        {QStringLiteral("context"), QJsonObject{{QStringLiteral("diagnostics"), onLine}}}};
    const QPointer<LspClient> server(client);
    client->request(QStringLiteral("textDocument/codeAction"), params,
                    [server, done](const QJsonValue &result, const QString &failure) {
                        QList<QJsonObject> actions;
                        for (const QJsonValue &item : result.toArray()) {
                            const QJsonObject action = item.toObject();
                            if (!action.contains(QStringLiteral("disabled")) &&
                                !action.value(QStringLiteral("title")).toString().isEmpty())
                                actions.append(action);
                        }
                        done(server, actions, failure);
                    });
}

// Appends to `paths` those of `more` it does not have yet.
void appendMissing(QStringList &paths, const QStringList &more)
{
    for (const QString &path : more) {
        if (!paths.contains(path))
            paths.append(path);
    }
}

// The files get_diagnostics checks: those asked for, else the open ones and
// every one a server or a build reported problems for.
QStringList pathsToCheck(const DuetToolContext &context, const QJsonObject &arguments)
{
    QStringList paths;
    for (const QJsonValue &value : arguments.value(QStringLiteral("paths")).toArray())
        paths.append(context.absolute(value.toString()));
    if (!paths.isEmpty())
        return paths;
    paths = context.tabs->openFilePaths();
    if (context.languageServers)
        appendMissing(paths, context.languageServers->filesWithDiagnostics());
    if (context.problems)
        appendMissing(paths, context.problems->filesWithBuildProblems());
    return paths;
}

void getDiagnostics(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QStringList paths = pathsToCheck(context, arguments);
    if (!context.languageServers) {
        reply(McpResult::error(QStringLiteral("Language servers are off in Duet")));
        return;
    }
    new DiagnosticsCollector(context, paths, reply); // deletes itself once it replied
}

void findDefinition(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    sendPositionalRequest(
        context, arguments,
        {.method = QStringLiteral("textDocument/definition"),
         .extraParams = {},
         .convert =
             [context](const QJsonValue &result) {
                 return McpResult::json(
                     QJsonObject{{QStringLiteral("definitions"), locationsJson(context, parseLocations(result))}});
             }},
        reply);
}

McpResult referencesResult(const DuetToolContext &context, const QJsonValue &result)
{
    const QList<Location> locations = parseLocations(result);
    return McpResult::json(QJsonObject{{QStringLiteral("references"), locationsJson(context, locations)},
                                       {QStringLiteral("count"), int(locations.size())}});
}

void findReferences(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const bool includeDeclaration = arguments.value(QStringLiteral("includeDeclaration")).toBool(true);
    sendPositionalRequest(
        context, arguments,
        {.method = QStringLiteral("textDocument/references"),
         .extraParams = {{QStringLiteral("context"),
                          QJsonObject{{QStringLiteral("includeDeclaration"), includeDeclaration}}}},
         .convert = [context](const QJsonValue &result) { return referencesResult(context, result); }},
        reply);
}

// The text of a hover's contents: a string, a markup object, or a list of
// either.
QString hoverText(const QJsonValue &contents)
{
    QStringList parts;
    const auto add = [&parts](const QJsonValue &part) {
        if (part.isString())
            parts.append(part.toString());
        else if (part.isObject())
            parts.append(part.toObject().value(QStringLiteral("value")).toString());
    };
    if (contents.isArray()) {
        for (const QJsonValue &part : contents.toArray())
            add(part);
    } else {
        add(contents);
    }
    return parts.join(QStringLiteral("\n\n")).trimmed();
}

McpResult hoverResult(const QJsonValue &result)
{
    const QString text = hoverText(result.toObject().value(QStringLiteral("contents")));
    return text.isEmpty() ? McpResult::message(QStringLiteral("Nothing known here")) : McpResult::message(text);
}

void getSymbolInfo(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    sendPositionalRequest(context, arguments,
                          {.method = QStringLiteral("textDocument/hover"), .extraParams = {}, .convert = hoverResult},
                          reply);
}

McpResult renameResult(const DuetToolContext &context, const QJsonValue &result)
{
    const WorkspaceEdit edit = parseWorkspaceEdit(result.toObject());
    if (edit.isEmpty())
        return McpResult::error(QStringLiteral("Nothing to rename here"));
    const int files = context.intelligence->applyWorkspaceEdit(edit);
    QJsonArray changed;
    int edits = 0;
    for (auto it = edit.cbegin(); it != edit.cend(); ++it) {
        changed.append(context.relative(it.key()));
        edits += int(it.value().size());
    }
    return McpResult::json(QJsonObject{
        {QStringLiteral("files"), changed}, {QStringLiteral("filesChanged"), files}, {QStringLiteral("edits"), edits}});
}

void renameSymbol(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString newName = arguments.value(QStringLiteral("newName")).toString().trimmed();
    if (newName.isEmpty()) {
        reply(McpResult::error(QStringLiteral("The new name is empty")));
        return;
    }
    sendPositionalRequest(context, arguments,
                          {.method = QStringLiteral("textDocument/rename"),
                           .extraParams = {{QStringLiteral("newName"), newName}},
                           .convert = [context](const QJsonValue &result) { return renameResult(context, result); }},
                          reply);
}

QJsonObject actionSummary(const QJsonObject &action)
{
    QJsonObject item{{QStringLiteral("title"), action.value(QStringLiteral("title")).toString()}};
    if (action.contains(QStringLiteral("kind")))
        item.insert(QStringLiteral("kind"), action.value(QStringLiteral("kind")).toString());
    if (action.value(QStringLiteral("isPreferred")).toBool())
        item.insert(QStringLiteral("preferred"), true);
    return item;
}

void getCodeActions(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    requestCodeActions(context, arguments,
                       [reply](LspClient *, const QList<QJsonObject> &actions, const QString &error) {
                           if (!error.isEmpty()) {
                               reply(McpResult::error(error));
                               return;
                           }
                           QJsonArray titles;
                           for (const QJsonObject &action : actions)
                               titles.append(actionSummary(action));
                           reply(McpResult::json(QJsonObject{{QStringLiteral("actions"), titles}}));
                       });
}

// The command an action carries out: its "command" object, or the action
// itself when it is a bare command; empty when it has none.
QJsonObject commandOf(const QJsonObject &action)
{
    const QJsonValue command = action.value(QStringLiteral("command"));
    if (command.isObject())
        return command.toObject();
    if (command.isString())
        return action;
    return {};
}

// Applies a code action's edit, then has the server carry out its command,
// which sends the edit back.
void applyAction(const DuetToolContext &context, LspClient *server, const QJsonObject &action, const McpReply &reply)
{
    int files = 0;
    if (action.contains(QStringLiteral("edit")))
        files = context.intelligence->applyWorkspaceEdit(
            parseWorkspaceEdit(action.value(QStringLiteral("edit")).toObject()));
    const QJsonObject command = commandOf(action);
    if (command.isEmpty() || !server) {
        reply(McpResult::json(QJsonObject{{QStringLiteral("applied"), action.value(QStringLiteral("title")).toString()},
                                          {QStringLiteral("filesChanged"), files}}));
        return;
    }
    server->request(QStringLiteral("workspace/executeCommand"),
                    QJsonObject{{QStringLiteral("command"), command.value(QStringLiteral("command"))},
                                {QStringLiteral("arguments"), command.value(QStringLiteral("arguments")).toArray()}},
                    [reply](const QJsonValue &, const QString &failure) {
                        reply(failure.isEmpty() ? McpResult::message(QStringLiteral("Applied"))
                                                : McpResult::error(failure));
                    });
}

void applyCodeAction(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString title = arguments.value(QStringLiteral("title")).toString();
    requestCodeActions(
        context, arguments,
        [context, reply, title](LspClient *server, const QList<QJsonObject> &actions, const QString &error) {
            if (!error.isEmpty()) {
                reply(McpResult::error(error));
                return;
            }
            const auto action = std::ranges::find_if(actions, [&title](const QJsonObject &candidate) {
                return candidate.value(QStringLiteral("title")).toString() == title;
            });
            if (action == actions.end()) {
                reply(McpResult::error(QStringLiteral("No action \"%1\" here (see get_code_actions)").arg(title)));
                return;
            }
            applyAction(context, server, *action, reply);
        });
}

// Writes `text` to `path` in the file's own encoding (BOM, ISO-8859-1).
bool writeText(const QString &path, const QString &text, QString *error)
{
    const std::optional<DecodedText> current = TextFile::load(path, error);
    return current && TextFile::save(path, text, current->format, error);
}

// Puts the formatter's text in the file's editor, or on disk when it is
// not open.
McpResult formattedResult(const DuetToolContext &context,
                          const QString &path,
                          const QString &text,
                          const McpResult &result)
{
    if (result.isError)
        return result;
    const QString formatted = result.structured.toObject().value(QStringLiteral("text")).toString();
    if (formatted == text)
        return McpResult::message(QStringLiteral("Already formatted"));
    // Typed or written meanwhile: the formatted text would erase it.
    if (textOf(context, path) != text)
        return McpResult::error(
            QStringLiteral("%1 changed while it was formatted: nothing applied").arg(context.relative(path)));
    if (EditorWidget *editor = context.tabs->editorFor(path)) {
        editor->applyFormattedText(formatted);
    } else if (QString error; !writeText(path, formatted, &error)) {
        return McpResult::error(error);
    }
    return McpResult::message(QStringLiteral("Formatted %1").arg(context.relative(path)));
}

void formatFile(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
    const std::optional<CodeFormatter::Command> command =
        CodeFormatter::find(LanguageRegistry::languageForFile(path), path);
    if (!command) {
        reply(McpResult::error(QStringLiteral("No formatter for this kind of file")));
        return;
    }
    const QString text = textOf(context, path);
    const QString folder = context.folder();
    inBackground(
        [command = *command, folder, text] {
            QString error;
            const std::optional<QString> formatted = CodeFormatter::run(command, folder, text, &error);
            if (!formatted)
                return McpResult::error(error);
            return McpResult::json(QJsonObject{{QStringLiteral("text"), *formatted}});
        },
        [context, path, text, reply](const McpResult &result) { reply(formattedResult(context, path, text, result)); });
}

std::optional<QString> readDiagnosticsResource(const DuetToolContext &context)
{
    QStringList paths;
    if (context.languageServers)
        paths = context.languageServers->filesWithDiagnostics();
    if (context.problems)
        appendMissing(paths, context.problems->filesWithBuildProblems());
    QJsonArray files;
    for (const QString &path : std::as_const(paths)) {
        QJsonArray problems =
            context.languageServers ? diagnosticsJson(context.languageServers->diagnostics(path)) : QJsonArray();
        for (const QJsonValue &problem : buildProblemsJson(context, path))
            problems.append(problem);
        if (!problems.isEmpty())
            files.append(
                QJsonObject{{QStringLiteral("file"), context.relative(path)}, {QStringLiteral("problems"), problems}});
    }
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{QStringLiteral("files"), files}}).toJson(QJsonDocument::Compact));
}

} // namespace

void registerCodeTools(McpRegistry &registry, const DuetToolContext &context)
{
    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_diagnostics"),
         .title = QStringLiteral("Problems"),
         .description = QStringLiteral(
             "Errors and warnings of files, from Duet's language servers (clangd, jdtls, tsserver, pyright…) on the "
             "text the user has, unsaved changes included, and from the output of the last builds the user ran in "
             "Duet (source \"build: <configuration>\"). Without paths: the open files and every file a server or a "
             "build reported problems for. Use it to check your edits compile."),
         .inputSchema = object({{QStringLiteral("paths"),
                                 array(string(QStringLiteral("File")), QStringLiteral("Files to check (optional)"))}})},
        handledBy(context, getDiagnostics)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("find_definition"),
         .title = QStringLiteral("Definition"),
         .description = QStringLiteral("Where the symbol at a position is defined, according to the file's language "
                                       "server (types and overloads resolved, unlike a text search)."),
         .inputSchema = positionSchema()},
        handledBy(context, findDefinition)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("find_references"),
         .title = QStringLiteral("Usages"),
         .description = QStringLiteral("Every usage of the symbol at a position in the project, according to the "
                                       "file's language server, with the text of each line."),
         .inputSchema = positionSchema({{QStringLiteral("includeDeclaration"),
                                         boolean(QStringLiteral("Include the declaration (default true)"))}},
                                       {QStringLiteral("includeDeclaration")})},
        handledBy(context, findReferences)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_symbol_info"),
         .title = QStringLiteral("Symbol information"),
         .description = QStringLiteral("The type, signature and documentation of the symbol at a position, as the "
                                       "language server's hover gives them."),
         .inputSchema = positionSchema()},
        handledBy(context, getSymbolInfo)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("rename_symbol"),
         .title = QStringLiteral("Rename symbol"),
         .description = QStringLiteral(
             "Renames the symbol at a position everywhere it is used, through the language server (a real "
             "refactoring, not a text replacement). Open files change in their editor (one step the user can undo, "
             "unsaved), the others on disk."),
         .inputSchema = positionSchema({{QStringLiteral("newName"), string(QStringLiteral("The new name"))}})},
        handledBy(context, renameSymbol)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_code_actions"),
         .title = QStringLiteral("Quick fixes"),
         .description = QStringLiteral("The quick fixes and refactorings the language server offers at a position "
                                       "(for the problems on its line): their titles, to apply one with "
                                       "apply_code_action."),
         .inputSchema = positionSchema()},
        handledBy(context, getCodeActions)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("apply_code_action"),
         .title = QStringLiteral("Apply a quick fix"),
         .description = QStringLiteral("Applies the quick fix or refactoring of that title at a position (see "
                                       "get_code_actions). Open files change in their editor, unsaved."),
         .inputSchema = positionSchema({{QStringLiteral("title"), string(QStringLiteral("The action's title"))}})},
        handledBy(context, applyCodeAction)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("format_file"),
         .title = QStringLiteral("Format a file"),
         .description = QStringLiteral(
             "Formats a file with its language's formatter as Duet's Format Document does (clang-format, prettier, "
             "black…, with the project's configuration). An open file changes in its editor, unsaved; a closed one "
             "on disk."),
         .inputSchema =
             object({{QStringLiteral("path"), string(QStringLiteral("File, relative to the project or absolute"))}},
                    {QStringLiteral("path")})},
        handledBy(context, formatFile)));

    // The problems as a resource, updated when a server reports.
    registry.addResource({
        QStringLiteral("duet://diagnostics"),
        false,
        QStringLiteral("diagnostics"),
        QStringLiteral("Problems"),
        QStringLiteral("The errors and warnings the language servers and the last builds reported, by file"),
        QStringLiteral("application/json"),
        [context](const QString &) { return readDiagnosticsResource(context); },
    });
    McpRegistry *target = &registry;
    const auto notify = [target] { target->notifyResourceUpdated(QStringLiteral("duet://diagnostics")); };
    if (context.problems)
        QObject::connect(context.problems, &ProblemStore::changed, target, notify);
    else if (context.languageServers)
        QObject::connect(context.languageServers, &LanguageServers::diagnosticsChanged, target, notify);
}

} // namespace DuetTools
