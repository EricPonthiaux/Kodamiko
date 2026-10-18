// MCP tools for files and tabs.

#include "DuetTools.h"
#include "McpRegistry.h"

#include "claude/EditorContext.h"
#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"
#include "navigation/CodeNavigator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

QString DuetToolContext::absolute(const QString &path) const
{
    if (path.isEmpty())
        return {};
    const QString base = folder ? folder() : QString();
    return QDir::cleanPath(QFileInfo(path).isAbsolute() || base.isEmpty() ? path : QDir(base).filePath(path));
}

QString DuetToolContext::relative(const QString &path) const
{
    const QString base = folder ? folder() : QString();
    if (base.isEmpty())
        return path;
    const QString relativePath = QDir(base).relativeFilePath(path);
    return relativePath.startsWith(QLatin1String("..")) ? path : relativePath;
}

namespace DuetTools {

QString instructions()
{
    return QStringLiteral(
        "Duet is the editor the user works in, next to this session: these tools see what the user sees "
        "(open files with their unsaved changes, the selection, problems, diagrams, terminals, git) and act in "
        "the editor (open a file at a line, lay a diagram out…). Prefer them to reading files from disk when "
        "the user refers to what is in front of them (\"this\", \"the selection\", \"the diagram\"). Paths are "
        "relative to the project unless absolute.");
}

namespace {

using namespace McpSchema;

// The text of a file as the editor has it: its unsaved buffer if any, else
// the disk.
std::optional<QString> fileText(const DuetToolContext &context, const QString &path, bool *unsaved)
{
    const QHash<QString, QString> buffers = context.tabs->unsavedTexts();
    if (buffers.contains(path)) {
        *unsaved = true;
        return buffers.value(path);
    }
    *unsaved = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return QString::fromUtf8(file.readAll());
}

} // namespace

void registerEditorTools(McpRegistry &registry, const DuetToolContext &context)
{
    registry.instructions = instructions();

    registry.addTool({
        QStringLiteral("get_editor_state"),
        QStringLiteral("Editor state"),
        QStringLiteral("What the user has in front of them in Duet: the current file, the cursor, the selection "
                       "(with its text), the problems of that file, and the files with unsaved changes. Use it "
                       "when the user says \"this\", \"here\" or \"the selection\"."),
        object({}),
        object({{QStringLiteral("file"), string(QStringLiteral("Current file"))},
                {QStringLiteral("line"), integer(QStringLiteral("Cursor line, 1-based"))},
                {QStringLiteral("column"), integer(QStringLiteral("Cursor column, 1-based"))},
                {QStringLiteral("selection"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}},
                {QStringLiteral("problems"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
                {QStringLiteral("unsavedFiles"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}}}),
        true,
        false,
        true,
        false,
        {},
        [context](const QJsonObject &, McpReply reply, McpProgress) {
            const QString folder = context.folder ? context.folder() : QString();
            reply(McpResult::json(EditorContext::of(*context.tabs, folder).toJson()));
        },
    });

    registry.addTool({
        QStringLiteral("get_open_files"),
        QStringLiteral("Open files"),
        QStringLiteral("The files open in Duet's tabs, in tab order, the current one first marked, with whether "
                       "each has unsaved changes."),
        object({}),
        {},
        true,
        false,
        true,
        false,
        {},
        [context](const QJsonObject &, McpReply reply, McpProgress) {
            const QHash<QString, QString> unsaved = context.tabs->unsavedTexts();
            const QString current = context.tabs->currentFilePath();
            QJsonArray files;
            for (const QString &path : context.tabs->openFilePaths())
                files.append(QJsonObject{{QStringLiteral("path"), context.relative(path)},
                                         {QStringLiteral("current"), path == current},
                                         {QStringLiteral("unsaved"), unsaved.contains(path)}});
            reply(McpResult::json(QJsonObject{{QStringLiteral("files"), files}}));
        },
    });

    registry.addTool({
        QStringLiteral("get_file_text"),
        QStringLiteral("File text"),
        QStringLiteral("The text of a file as the user sees it in Duet: its unsaved changes included, which the "
                       "file on disk does not have yet. Lines can be limited to a range."),
        object({{QStringLiteral("path"), string(QStringLiteral("File, relative to the project or absolute"))},
                {QStringLiteral("startLine"), integer(QStringLiteral("First line, 1-based (optional)"))},
                {QStringLiteral("endLine"), integer(QStringLiteral("Last line, 1-based, included (optional)"))}},
               {QStringLiteral("path")}),
        {},
        true,
        false,
        true,
        false,
        {},
        [context](const QJsonObject &arguments, McpReply reply, McpProgress) {
            const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
            bool unsaved = false;
            const std::optional<QString> text = fileText(context, path, &unsaved);
            if (!text) {
                reply(McpResult::error(QStringLiteral("Cannot read %1").arg(context.relative(path))));
                return;
            }
            QStringList lines = text->split(QLatin1Char('\n'));
            const int first = std::max(1, arguments.value(QStringLiteral("startLine")).toInt(1));
            const int last =
                std::min(int(lines.size()), arguments.value(QStringLiteral("endLine")).toInt(int(lines.size())));
            lines = lines.mid(first - 1, std::max(0, last - first + 1));
            reply(McpResult::json(QJsonObject{{QStringLiteral("path"), context.relative(path)},
                                              {QStringLiteral("unsaved"), unsaved},
                                              {QStringLiteral("startLine"), first},
                                              {QStringLiteral("text"), lines.join(QLatin1Char('\n'))}}));
        },
    });

    registry.addTool({
        QStringLiteral("open_file"),
        QStringLiteral("Open file"),
        QStringLiteral("Opens a file in a Duet tab for the user (a diagram in the diagram editor), optionally at a "
                       "line, with a column and a length to select. Use it to show the user what you are talking "
                       "about."),
        object({{QStringLiteral("path"), string(QStringLiteral("File, relative to the project or absolute"))},
                {QStringLiteral("line"), integer(QStringLiteral("Line, 1-based (optional)"))},
                {QStringLiteral("column"), integer(QStringLiteral("Column, 1-based (optional)"))},
                {QStringLiteral("length"), integer(QStringLiteral("Characters to select from the column (optional)"))}},
               {QStringLiteral("path")}),
        {},
        false,
        false,
        true,
        false,
        {},
        [context](const QJsonObject &arguments, McpReply reply, McpProgress) {
            const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
            if (!QFileInfo(path).isFile()) {
                reply(McpResult::error(QStringLiteral("No file %1").arg(context.relative(path))));
                return;
            }
            const int line = arguments.value(QStringLiteral("line")).toInt(0);
            if (line > 0 && context.navigator) {
                context.navigator->goToLine(path, line - 1,
                                            std::max(0, arguments.value(QStringLiteral("column")).toInt(1) - 1),
                                            std::max(0, arguments.value(QStringLiteral("length")).toInt(0)));
            } else if (!context.tabs->openFile(path)) {
                reply(McpResult::error(QStringLiteral("Duet could not open %1").arg(context.relative(path))));
                return;
            }
            reply(McpResult::message(QStringLiteral("Opened %1").arg(context.relative(path))));
        },
    });

    registry.addTool({
        QStringLiteral("save_files"),
        QStringLiteral("Save files"),
        QStringLiteral("Saves files that have unsaved changes in Duet (all of them without paths). Ask the user "
                       "first unless they asked you to."),
        object({{QStringLiteral("paths"), array(string(QStringLiteral("File")), QStringLiteral("Files to save; all "
                                                                                               "when omitted"))}}),
        {},
        false,
        false,
        true,
        false,
        {},
        [context](const QJsonObject &arguments, McpReply reply, McpProgress) {
            const QJsonArray paths = arguments.value(QStringLiteral("paths")).toArray();
            if (paths.isEmpty()) {
                const bool saved = context.tabs->saveAll();
                reply(saved ? McpResult::message(QStringLiteral("Saved"))
                            : McpResult::error(QStringLiteral("Some files could not be saved")));
                return;
            }
            QStringList failed;
            for (const QJsonValue &value : paths) {
                const QString path = context.absolute(value.toString());
                EditorWidget *editor = context.tabs->editorFor(path);
                if (!editor) {
                    failed.append(context.relative(path));
                    continue;
                }
                QString error;
                if (editor->isModified() && !editor->save(&error))
                    failed.append(context.relative(path));
            }
            reply(failed.isEmpty()
                      ? McpResult::message(QStringLiteral("Saved"))
                      : McpResult::error(QStringLiteral("Not saved: %1").arg(failed.join(QStringLiteral(", ")))));
        },
    });

    registry.addTool({
        QStringLiteral("show_diff"),
        QStringLiteral("Show a diff"),
        QStringLiteral("Shows a unified diff in a Duet tab, for the user to read (a change you propose, or one you "
                       "explain). It changes no file."),
        object({{QStringLiteral("title"), string(QStringLiteral("Tab title"))},
                {QStringLiteral("diff"), string(QStringLiteral("Unified diff text"))}},
               {QStringLiteral("title"), QStringLiteral("diff")}),
        {},
        false,
        false,
        false,
        false,
        {},
        [context](const QJsonObject &arguments, McpReply reply, McpProgress) {
            context.tabs->showDiff(arguments.value(QStringLiteral("title")).toString(),
                                   arguments.value(QStringLiteral("diff")).toString());
            reply(McpResult::message(QStringLiteral("Shown")));
        },
    });

    // Prompts: slash commands in the Claude session (/mcp__duet__…).
    registry.addPrompt(
        {QStringLiteral("review-selection"),
         QStringLiteral("Review the selection"),
         QStringLiteral("Review the code selected in Duet"),
         {{QStringLiteral("focus"), QStringLiteral("What to look at (bugs, naming, performance…)"), false}},
         [](const QJsonObject &arguments) {
             const QString focus = arguments.value(QStringLiteral("focus")).toString();
             return QStringLiteral("Get the code I selected in Duet (get_editor_state) and review it%1: "
                                   "point out problems with their line, then propose fixes, applying "
                                   "nothing until I agree.")
                 .arg(focus.isEmpty() ? QString() : QStringLiteral(", looking at ") + focus);
         }});
    registry.addPrompt({QStringLiteral("fix-diagnostics"),
                        QStringLiteral("Fix the problems"),
                        QStringLiteral("Fix the errors the language servers report"),
                        {{QStringLiteral("path"), QStringLiteral("File (optional: the open files)"), false}},
                        [](const QJsonObject &arguments) {
                            const QString path = arguments.value(QStringLiteral("path")).toString();
                            return QStringLiteral(
                                       "Get the problems Duet's language servers report%1 (get_diagnostics), "
                                       "fix the errors, then check again with get_diagnostics until none "
                                       "is left. Tell me what you changed.")
                                .arg(path.isEmpty() ? QString() : QStringLiteral(" for ") + path);
                        }});
    registry.addPrompt({QStringLiteral("explain-symbol"),
                        QStringLiteral("Explain the symbol"),
                        QStringLiteral("Explain the symbol at the cursor: what it is, where it is used"),
                        {},
                        [](const QJsonObject &) {
                            return QStringLiteral("Take the symbol at my cursor in Duet (get_editor_state), find its "
                                                  "definition and usages (get_symbol_info, find_definition, "
                                                  "find_references) and explain what it does and how the project "
                                                  "uses it, briefly.");
                        }});

    // The editor's state as a resource: clients that subscribe hear when
    // the user moves.
    registry.addResource({
        QStringLiteral("duet://editor/state"),
        false,
        QStringLiteral("editor-state"),
        QStringLiteral("Editor state"),
        QStringLiteral("The current file, cursor, selection, problems and unsaved files in Duet"),
        QStringLiteral("application/json"),
        [context](const QString &) -> std::optional<QString> {
            const QString folder = context.folder ? context.folder() : QString();
            return QString::fromUtf8(
                QJsonDocument(EditorContext::of(*context.tabs, folder).toJson()).toJson(QJsonDocument::Compact));
        },
    });
    registry.addResource({
        QStringLiteral("duet://file/{path}"),
        true,
        QStringLiteral("file"),
        QStringLiteral("File as in the editor"),
        QStringLiteral("A file's text with its unsaved changes"),
        QStringLiteral("text/plain"),
        [context](const QString &uri) -> std::optional<QString> {
            const QString path = context.absolute(uri.mid(QStringLiteral("duet://file/").size()));
            bool unsaved = false;
            return fileText(context, path, &unsaved);
        },
    });
}

} // namespace DuetTools
