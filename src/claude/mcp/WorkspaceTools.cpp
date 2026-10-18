// MCP tools for the project as a whole: search, files, symbols, the
// editor's commands, the project's setup.

#include "DuetTools.h"
#include "McpRegistry.h"
#include "McpWork.h"
#include "editor/EditorTabs.h"
#include "lsp/LanguageServers.h"
#include "navigation/GoToPopup.h"
#include "navigation/SymbolIndex.h"
#include "outline/IOutlineProvider.h"
#include "outline/OutlineProviders.h"
#include "projects/ProjectDetector.h"
#include "search/FindInFiles.h"
#include "settings/Keymap.h"
#include "syntax/LanguageRegistry.h"
#include "workspace/ProjectFiles.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QMenuBar>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace DuetTools {

namespace {

using namespace McpSchema;

QString kindName(SymbolKind kind)
{
    switch (kind) {
    case SymbolKind::Namespace:
        return QStringLiteral("namespace");
    case SymbolKind::Class:
        return QStringLiteral("class");
    case SymbolKind::Struct:
        return QStringLiteral("struct");
    case SymbolKind::Interface:
        return QStringLiteral("interface");
    case SymbolKind::Enum:
        return QStringLiteral("enum");
    case SymbolKind::Type:
        return QStringLiteral("type");
    case SymbolKind::Function:
        return QStringLiteral("function");
    case SymbolKind::Method:
        return QStringLiteral("method");
    case SymbolKind::Field:
        return QStringLiteral("field");
    case SymbolKind::Macro:
        return QStringLiteral("macro");
    case SymbolKind::Heading:
        return QStringLiteral("heading");
    case SymbolKind::Target:
        return QStringLiteral("target");
    }
    return {};
}

QJsonArray outlineJson(const std::vector<OutlineSymbol> &symbols)
{
    QJsonArray array;
    for (const OutlineSymbol &symbol : symbols) {
        QJsonObject item{{QStringLiteral("name"), symbol.name},
                         {QStringLiteral("kind"), kindName(symbol.kind)},
                         {QStringLiteral("startLine"), symbol.startLine + 1},
                         {QStringLiteral("endLine"), symbol.endLine + 1}};
        if (symbol.declarationOnly)
            item.insert(QStringLiteral("declarationOnly"), true);
        if (!symbol.children.empty())
            item.insert(QStringLiteral("children"), outlineJson(symbol.children));
        array.append(item);
    }
    return array;
}

// Commands that would end the session or the editor: not for Claude.
bool isForbiddenCommand(const QString &id)
{
    static const QStringList forbidden{QStringLiteral("Quit"), QStringLiteral("Exit"), QStringLiteral("Session"),
                                       QStringLiteral("Restart"), QStringLiteral("Close All")};
    return std::ranges::any_of(forbidden,
                               [&id](const QString &word) { return id.contains(word, Qt::CaseInsensitive); });
}

FindQuery findQueryFrom(const QJsonObject &arguments)
{
    FindQuery query;
    query.text = arguments.value(QStringLiteral("text")).toString();
    query.regularExpression = arguments.value(QStringLiteral("regex")).toBool();
    query.matchCase = arguments.value(QStringLiteral("matchCase")).toBool();
    query.wholeWords = arguments.value(QStringLiteral("wholeWords")).toBool();
    query.fileMask = arguments.value(QStringLiteral("fileMask")).toString();
    return query;
}

void searchInFiles(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const FindQuery query = findQueryFrom(arguments);
    QString error;
    if (!query.compile(&error)) {
        reply(McpResult::error(error));
        return;
    }
    const int limit = std::clamp(arguments.value(QStringLiteral("limit")).toInt(200), 1, FindInFilesSearch::matchLimit);
    auto *search = new FindInFilesSearch;
    QObject::connect(
        search, &FindInFilesSearch::finished, search,
        [search, reply, context, limit](const QList<FindMatch> &matches, bool truncated, int files) {
            QJsonArray found;
            for (const FindMatch &match : matches.mid(0, limit))
                found.append(QJsonObject{{QStringLiteral("file"), context.relative(match.filePath)},
                                         {QStringLiteral("line"), match.line + 1},
                                         {QStringLiteral("column"), match.column + 1},
                                         {QStringLiteral("text"), match.lineText.trimmed()}});
            reply(McpResult::json(QJsonObject{{QStringLiteral("matches"), found},
                                              {QStringLiteral("truncated"), truncated || matches.size() > limit},
                                              {QStringLiteral("filesSearched"), files}}));
            search->deleteLater();
        });
    search->start(context.folder(), query, context.tabs->unsavedTexts());
}

// The project's files whose path matches `query`, best first, as Go to File
// ranks them; the total found.
McpResult filesMatching(const QString &folder, const QString &query, int limit)
{
    struct Scored {
        int score;
        QString path;
    };

    std::vector<Scored> scored;
    const QDir root(folder);
    for (const QFileInfo &file : ProjectFiles::list(folder)) {
        const QString relative = root.relativeFilePath(file.absoluteFilePath());
        const int score = GoToPopup::fileScore(relative, query);
        if (score > 0)
            scored.push_back({score, relative});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) { return a.score > b.score; });
    QJsonArray files;
    for (size_t i = 0; i < scored.size() && int(i) < limit; ++i)
        files.append(scored[i].path);
    return McpResult::json(
        QJsonObject{{QStringLiteral("files"), files}, {QStringLiteral("total"), int(scored.size())}});
}

void findFiles(const DuetToolContext &context, const QJsonObject &arguments, const McpReply &reply)
{
    const QString folder = context.folder();
    const QString query = arguments.value(QStringLiteral("query")).toString();
    const int limit = std::clamp(arguments.value(QStringLiteral("limit")).toInt(50), 1, 1000);
    inBackground([folder, query, limit] { return filesMatching(folder, query, limit); }, reply);
}

McpResult getDocumentSymbols(const DuetToolContext &context, const QJsonObject &arguments)
{
    const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
    const std::unique_ptr<IOutlineProvider> provider =
        OutlineProviders::create(LanguageRegistry::languageForFile(path));
    if (!provider)
        return McpResult::error(QStringLiteral("Duet has no outline for this kind of file"));
    QByteArray text;
    const QHash<QString, QString> buffers = context.tabs->unsavedTexts();
    if (buffers.contains(path)) {
        text = buffers.value(path).toUtf8();
    } else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return McpResult::error(QStringLiteral("Cannot read %1").arg(context.relative(path)));
        text = file.readAll();
    }
    return McpResult::json(QJsonObject{{QStringLiteral("path"), context.relative(path)},
                                       {QStringLiteral("symbols"), outlineJson(provider->symbols(text))}});
}

McpResult findSymbol(const DuetToolContext &context, const QJsonObject &arguments)
{
    if (!context.symbols)
        return McpResult::error(QStringLiteral("No symbol index"));
    QJsonArray found;
    for (const SymbolLocation &symbol : context.symbols->lookup(arguments.value(QStringLiteral("name")).toString())) {
        found.append(QJsonObject{{QStringLiteral("name"), symbol.name},
                                 {QStringLiteral("container"), symbol.container},
                                 {QStringLiteral("kind"), kindName(symbol.kind)},
                                 {QStringLiteral("declarationOnly"), symbol.declarationOnly},
                                 {QStringLiteral("file"), context.relative(symbol.filePath)},
                                 {QStringLiteral("startLine"), symbol.startLine + 1},
                                 {QStringLiteral("endLine"), symbol.endLine + 1}});
    }
    return McpResult::json(
        QJsonObject{{QStringLiteral("symbols"), found}, {QStringLiteral("indexing"), context.symbols->isIndexing()}});
}

QJsonObject commandJson(const Keymap::Command &command)
{
    QJsonObject item{{QStringLiteral("id"), command.id}, {QStringLiteral("enabled"), command.action->isEnabled()}};
    if (command.action->isCheckable())
        item.insert(QStringLiteral("checked"), command.action->isChecked());
    if (!command.action->shortcut().isEmpty())
        item.insert(QStringLiteral("shortcut"), command.action->shortcut().toString(QKeySequence::PortableText));
    return item;
}

McpResult listCommands(const DuetToolContext &context, const QJsonObject &)
{
    QJsonArray commands;
    if (context.menus) {
        for (const Keymap::Command &command : Keymap::commands(context.menus)) {
            if (!isForbiddenCommand(command.id))
                commands.append(commandJson(command));
        }
    }
    return McpResult::json(QJsonObject{{QStringLiteral("commands"), commands}});
}

McpResult runCommand(const DuetToolContext &context, const QJsonObject &arguments)
{
    const QString id = arguments.value(QStringLiteral("id")).toString();
    if (isForbiddenCommand(id))
        return McpResult::error(QStringLiteral("This command is not available to Claude"));
    for (const Keymap::Command &command : Keymap::commands(context.menus)) {
        if (command.id != id)
            continue;
        if (!command.action->isEnabled())
            return McpResult::error(QStringLiteral("\"%1\" is not available now").arg(id));
        // After the reply: a command may open a modal popup.
        QTimer::singleShot(0, command.action, &QAction::trigger);
        return McpResult::message(QStringLiteral("Ran %1").arg(command.title));
    }
    return McpResult::error(QStringLiteral("No command \"%1\" (see list_commands)").arg(id));
}

McpResult projectInfo(const QString &folder, const QStringList &servers)
{
    QJsonArray languages;
    for (const DetectedLanguage &language : ProjectDetector::detect(folder))
        languages.append(QJsonObject{{QStringLiteral("language"), language.language},
                                     {QStringLiteral("runtimeVersion"), language.runtimeVersion},
                                     {QStringLiteral("evidence"), language.evidence}});
    return McpResult::json(QJsonObject{{QStringLiteral("folder"), folder},
                                       {QStringLiteral("name"), QDir(folder).dirName()},
                                       {QStringLiteral("languages"), languages},
                                       {QStringLiteral("languageServers"), QJsonArray::fromStringList(servers)}});
}

void getProjectInfo(const DuetToolContext &context, const QJsonObject &, const McpReply &reply)
{
    const QString folder = context.folder();
    const QStringList servers = context.languageServers ? context.languageServers->describe() : QStringList();
    inBackground([folder, servers] { return projectInfo(folder, servers); }, reply);
}

McpResult notifyUser(const DuetToolContext &context, const QJsonObject &arguments)
{
    if (context.showMessage)
        context.showMessage(arguments.value(QStringLiteral("message")).toString());
    return McpResult::message(QStringLiteral("Shown"));
}

} // namespace

void registerWorkspaceTools(McpRegistry &registry, const DuetToolContext &context)
{
    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("search_in_files"),
         .title = QStringLiteral("Search in files"),
         .description = QStringLiteral("Searches the project's files as Duet's Find in Files does: unsaved changes "
                                       "included, files ignored by git left out. Returns the matching lines (1-based) "
                                       "with their file."),
         .inputSchema =
             object({{QStringLiteral("text"), string(QStringLiteral("Text or regular expression to find"))},
                     {QStringLiteral("regex"), boolean(QStringLiteral("The text is a regular expression"))},
                     {QStringLiteral("matchCase"), boolean(QStringLiteral("Case sensitive"))},
                     {QStringLiteral("wholeWords"), boolean(QStringLiteral("Whole words only"))},
                     {QStringLiteral("fileMask"),
                      string(QStringLiteral("File patterns, comma separated (\"*.cpp, *.h\"); \"!\" excludes"))},
                     {QStringLiteral("limit"), integer(QStringLiteral("Most matches returned (default 200)"))}},
                    {QStringLiteral("text")})},
        handledBy(context, searchInFiles)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("find_files"),
         .title = QStringLiteral("Find files"),
         .description = QStringLiteral("Finds project files by name, as Duet's Go to File does (fuzzy: \"diagview\" "
                                       "finds DiagramView.cpp; a slash matches folders). Files ignored by git are "
                                       "left out."),
         .inputSchema = object({{QStringLiteral("query"), string(QStringLiteral("Part of a file name or path"))},
                                {QStringLiteral("limit"), integer(QStringLiteral("Most files returned (default 50)"))}},
                               {QStringLiteral("query")})},
        handledBy(context, findFiles)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_document_symbols"),
         .title = QStringLiteral("Document symbols"),
         .description = QStringLiteral("The outline of a file as Duet's Outline pane shows it (classes, functions, "
                                       "methods, headings…), with their lines, nested. Works on unsaved text."),
         .inputSchema =
             object({{QStringLiteral("path"), string(QStringLiteral("File, relative to the project or absolute"))}},
                    {QStringLiteral("path")})},
        handledBy(context, getDocumentSymbols)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("find_symbol"),
         .title = QStringLiteral("Find symbol"),
         .description = QStringLiteral(
             "Where a class, function, method, field or macro is declared and defined in the project, from Duet's "
             "symbol index (names only, no type information: use find_definition for the symbol under a position)."),
         .inputSchema = object({{QStringLiteral("name"), string(QStringLiteral("Simple name: \"bar\" for Foo::bar"))}},
                               {QStringLiteral("name")})},
        handledBy(context, findSymbol)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("list_commands"),
         .title = QStringLiteral("Editor commands"),
         .description = QStringLiteral("The commands of Duet's menus (ids such as \"Edit/Format Document\"), with "
                                       "their shortcuts and whether they are available now: what run_command can "
                                       "trigger."),
         .inputSchema = object({})},
        handledBy(context, listCommands)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("run_command"),
         .title = QStringLiteral("Run an editor command"),
         .description = QStringLiteral("Triggers one of Duet's menu commands by id (see list_commands), as if the "
                                       "user chose it: most act on the current file or open a panel for the user."),
         .inputSchema = object({{QStringLiteral("id"), string(QStringLiteral("Command id, \"Edit/Format Document\""))}},
                               {QStringLiteral("id")})},
        handledBy(context, runCommand)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_project_info"),
         .title = QStringLiteral("Project information"),
         .description = QStringLiteral("The project Duet has open: its folder, the languages detected (with runtime "
                                       "versions and the files that tell), and the state of the language servers."),
         .inputSchema = object({})},
        handledBy(context, getProjectInfo)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("notify_user"),
         .title = QStringLiteral("Tell the user"),
         .description = QStringLiteral("Shows a short message in Duet's status bar, for something the user should "
                                       "notice in the editor while you work."),
         .inputSchema = object({{QStringLiteral("message"), string(QStringLiteral("One short sentence"))}},
                               {QStringLiteral("message")})},
        handledBy(context, notifyUser)));
}

} // namespace DuetTools
