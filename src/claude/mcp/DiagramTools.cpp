// MCP tools for diagrams: what the user sees and selects in a diagram, and
// changes made in the open diagram (its source applied, its layout, its
// selection, an export).

#include "DuetTools.h"
#include "McpRegistry.h"
#include "editor/EditorTabs.h"
#include "viewers/DiagramCheck.h"
#include "viewers/DiagramKinds.h"
#include "viewers/DiagramView.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Canvas.h"
#include "viewers/diagram/Document.h"
#include "viewers/diagram/Family.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

namespace DuetTools {

namespace {

using namespace McpSchema;

QJsonObject pathArgument()
{
    return string(QStringLiteral("Diagram file (.mmd, .puml, .board, .flow), relative to the project or absolute"));
}

// The diagram open in a tab for `path`, opening it if needed; null, with
// `error`, if it is not a diagram Duet reads.
DiagramView *openDiagram(const DuetToolContext &context, const QString &path, QString *error)
{
    if (DiagramView *view = context.tabs->diagramViewFor(path))
        return view;
    if (!DiagramView::isDiagramFile(path) || !QFileInfo(path).isFile()) {
        *error = QStringLiteral("%1 is not a diagram file").arg(context.relative(path));
        return nullptr;
    }
    context.tabs->openFile(path);
    if (DiagramView *view = context.tabs->diagramViewFor(path))
        return view;
    *error = QStringLiteral("Duet cannot read %1 as a diagram").arg(context.relative(path));
    return nullptr;
}

QJsonObject boardJson(const diagram::Board &board, const diagram::Codec &codec)
{
    QJsonArray elements;
    for (const auto &s : board.shapes()) {
        QJsonObject element{{QStringLiteral("key"), s->key.isEmpty() ? s->id : s->key},
                            {QStringLiteral("type"), s->type},
                            {QStringLiteral("text"), s->displayText()},
                            {QStringLiteral("x"), qRound(s->x)},
                            {QStringLiteral("y"), qRound(s->y)},
                            {QStringLiteral("width"), qRound(s->w)},
                            {QStringLiteral("height"), qRound(s->h)}};
        if (!s->parentBoundary.isEmpty())
            element.insert(QStringLiteral("parent"), s->parentBoundary);
        if (board.selection().contains(s->id))
            element.insert(QStringLiteral("selected"), true);
        elements.append(element);
    }
    const auto keyOf = [&board](const QString &id) {
        const diagram::Shape *s = board.shape(id);
        return s ? (s->key.isEmpty() ? s->id : s->key) : id;
    };
    QJsonArray links;
    for (const auto &l : board.links()) {
        QJsonObject link{{QStringLiteral("from"), keyOf(l->from)},
                         {QStringLiteral("to"), keyOf(l->to)},
                         {QStringLiteral("type"), l->type},
                         {QStringLiteral("label"), l->label}};
        if (board.linkSelection().contains(l->id))
            link.insert(QStringLiteral("selected"), true);
        links.append(link);
    }
    return QJsonObject{{QStringLiteral("family"), board.family() ? board.family()->id : QString()},
                       {QStringLiteral("notation"), codec.language()},
                       {QStringLiteral("source"), diagram::document::source(board, codec)},
                       {QStringLiteral("elements"), elements},
                       {QStringLiteral("links"), links}};
}

} // namespace

void registerDiagramTools(McpRegistry &registry, const DuetToolContext &context)
{
    registry.addTool(McpTool::reading(
        QStringLiteral("list_diagram_kinds"), QStringLiteral("Diagram kinds"),
        QStringLiteral("The kinds of diagram Duet's editor draws, with the file extension and a blank source of "
                       "each (follow the duet:diagrams skill to write one)."),
        object({}), McpTool::immediate([](const QJsonObject &) {
            QJsonArray kinds;
            for (const DiagramKind &kind : DiagramKinds::available())
                kinds.append(QJsonObject{{QStringLiteral("id"), kind.id},
                                         {QStringLiteral("title"), kind.title},
                                         {QStringLiteral("extension"), kind.extension},
                                         {QStringLiteral("blank"), QString::fromUtf8(kind.blank)}});
            return McpResult::json(QJsonObject{{QStringLiteral("kinds"), kinds}});
        })));

    registry.addTool(McpTool::reading(
        QStringLiteral("get_diagram"), QStringLiteral("Diagram"),
        QStringLiteral("A diagram as Duet's editor has it: its kind, its source (with unsaved changes), its "
                       "elements with their place and size, its links, and what the user has selected in it. Use "
                       "it when the user talks about \"this element\" or \"the selected ones\" of a diagram."),
        object({{QStringLiteral("path"), pathArgument()}}, {QStringLiteral("path")}),
        McpTool::immediate([context](const QJsonObject &arguments) {
            const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
            if (DiagramView *view = context.tabs->diagramViewFor(path)) {
                QJsonObject json = boardJson(*view->board(), *view->codec());
                json.insert(QStringLiteral("open"), true);
                json.insert(QStringLiteral("unsaved"), view->isModified());
                return McpResult::json(json);
            }
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return McpResult::error(QStringLiteral("Cannot read %1").arg(context.relative(path)));
            diagram::Board board;
            diagram::document::Loaded loaded;
            QString error;
            if (!diagram::document::load(board, path, file.readAll(), loaded, &error))
                return McpResult::error(error);
            QJsonObject json = boardJson(board, *loaded.codec);
            json.insert(QStringLiteral("open"), false);
            return McpResult::json(json);
        })));

    registry.addTool(McpTool::reading(
        QStringLiteral("get_diagram_problems"), QStringLiteral("Diagram problems"),
        QStringLiteral("What Duet's diagram editor cannot read in a diagram file (an unsupported kind, statements "
                       "it does not understand): empty when the diagram reads as written."),
        object({{QStringLiteral("path"), pathArgument()}}, {QStringLiteral("path")}),
        McpTool::immediate([context](const QJsonObject &arguments) {
            const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
            QByteArray content;
            if (DiagramView *view = context.tabs->diagramViewFor(path)) {
                content = view->content();
            } else {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly))
                    return McpResult::error(QStringLiteral("Cannot read %1").arg(context.relative(path)));
                content = file.readAll();
            }
            return McpResult::json(QJsonObject{
                {QStringLiteral("problems"), QJsonArray::fromStringList(DiagramCheck::problems(path, content))}});
        })));

    registry.addTool(McpTool::acting(
        QStringLiteral("apply_diagram_source"), QStringLiteral("Change a diagram"),
        QStringLiteral("Replaces an open diagram's content with a new source in the same notation, in Duet's "
                       "editor (one step the user can undo; elements keep their place by id, new ones are placed "
                       "beside their neighbors). The file is not written: the user saves. Use it to change a "
                       "diagram the user is looking at; to create one, write the file."),
        object({{QStringLiteral("path"), pathArgument()},
                {QStringLiteral("source"), string(QStringLiteral("The whole new source, without the \"duet\" "
                                                                 "layout comments"))}},
               {QStringLiteral("path"), QStringLiteral("source")}),
        McpTool::immediate([context](const QJsonObject &arguments) {
            const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
            QString error;
            DiagramView *view = openDiagram(context, path, &error);
            if (!view)
                return McpResult::error(error);
            diagram::document::Loaded loaded;
            if (!diagram::document::applySource(*view->board(), *view->codec(),
                                                arguments.value(QStringLiteral("source")).toString(), loaded, &error))
                return McpResult::error(error);
            view->canvas()->settle(loaded, true);
            QJsonObject json{{QStringLiteral("elements"), int(view->board()->shapes().size())},
                             {QStringLiteral("links"), int(view->board()->links().size())}};
            if (!loaded.warnings.isEmpty())
                json.insert(QStringLiteral("problems"), QJsonArray::fromStringList(loaded.warnings));
            return McpResult::json(json);
        })));

    registry.addTool(McpTool::acting(
        QStringLiteral("layout_diagram"), QStringLiteral("Lay a diagram out"),
        QStringLiteral("Lays an open diagram out again automatically in Duet's editor (the user's arrangement is "
                       "replaced; one step they can undo)."),
        object({{QStringLiteral("path"), pathArgument()},
                {QStringLiteral("direction"),
                 enumeration({QStringLiteral("TB"), QStringLiteral("LR"), QStringLiteral("BT"), QStringLiteral("RL")},
                             QStringLiteral("Reading direction, for the kinds that "
                                            "have one (optional)"))}},
               {QStringLiteral("path")}),
        McpTool::immediate([context](const QJsonObject &arguments) {
            const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
            QString error;
            DiagramView *view = openDiagram(context, path, &error);
            if (!view)
                return McpResult::error(error);
            if (arguments.contains(QStringLiteral("direction")))
                view->board()->meta.insert(QStringLiteral("rankdir"),
                                           arguments.value(QStringLiteral("direction")).toString());
            if (!view->canvas()->arrange(true))
                return McpResult::error(QStringLiteral("The layout failed"));
            return McpResult::message(QStringLiteral("Laid out"));
        })));

    registry.addTool(McpTool::acting(
        QStringLiteral("select_diagram_elements"), QStringLiteral("Select in a diagram"),
        QStringLiteral("Selects elements of an open diagram by key (their source ids), to show them to the user; "
                       "an empty list clears the selection."),
        object({{QStringLiteral("path"), pathArgument()},
                {QStringLiteral("keys"), array(string(QStringLiteral("Element key")), QStringLiteral("Elements to "
                                                                                                     "select"))}},
               {QStringLiteral("path"), QStringLiteral("keys")}),
        McpTool::immediate([context](const QJsonObject &arguments) {
            const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
            QString error;
            DiagramView *view = openDiagram(context, path, &error);
            if (!view)
                return McpResult::error(error);
            diagram::Board *board = view->board();
            board->clearSelection();
            QStringList missing;
            for (const QJsonValue &key : arguments.value(QStringLiteral("keys")).toArray()) {
                if (const diagram::Shape *s = board->shapeByReference(key.toString()))
                    board->selectShape(s->id, true);
                else
                    missing.append(key.toString());
            }
            return missing.isEmpty()
                       ? McpResult::message(QStringLiteral("Selected"))
                       : McpResult::error(QStringLiteral("No element %1").arg(missing.join(QStringLiteral(", "))));
        })));

    registry.addTool(McpTool::acting(
        QStringLiteral("export_diagram"), QStringLiteral("Export a diagram"),
        QStringLiteral("Writes a diagram as an image, SVG or PNG by the output's extension, in a light printable "
                       "palette (for documentation)."),
        object({{QStringLiteral("path"), pathArgument()},
                {QStringLiteral("output"), string(QStringLiteral("Image file to write (.svg or .png)"))}},
               {QStringLiteral("path"), QStringLiteral("output")}),
        McpTool::immediate([context](const QJsonObject &arguments) {
            const QString path = context.absolute(arguments.value(QStringLiteral("path")).toString());
            const QString output = context.absolute(arguments.value(QStringLiteral("output")).toString());
            QString error;
            DiagramView *view = openDiagram(context, path, &error);
            if (!view)
                return McpResult::error(error);
            if (!view->exportImage(output, &error))
                return McpResult::error(error);
            return McpResult::message(QStringLiteral("Wrote %1").arg(context.relative(output)));
        })));

    registry.addPrompt(
        {QStringLiteral("draw-diagram"),
         QStringLiteral("Draw a diagram"),
         QStringLiteral("Draw a diagram of part of the project, opened in Duet's diagram editor"),
         {{QStringLiteral("subject"), QStringLiteral("What it shows (\"the login flow in src/auth\")"), true},
          {QStringLiteral("kind"),
           QStringLiteral("Kind: flowchart, sequence, class, state, data model, "
                          "C4, mind map… (optional)"),
           false}},
         [](const QJsonObject &arguments) {
             const QString kind = arguments.value(QStringLiteral("kind")).toString();
             return QStringLiteral("Following the duet:diagrams skill, draw %1 of %2: read the code it "
                                   "refers to first and draw what it really does. Then open it in Duet "
                                   "(open_file) and check it reads cleanly (get_diagram_problems).")
                 .arg(kind.isEmpty() ? QStringLiteral("the most fitting diagram")
                                     : QStringLiteral("a %1 diagram").arg(kind),
                      arguments.value(QStringLiteral("subject")).toString());
         }});

    registry.addResource({
        QStringLiteral("duet://diagram/{path}"),
        true,
        QStringLiteral("diagram"),
        QStringLiteral("Diagram as in the editor"),
        QStringLiteral("A diagram's source with its unsaved changes"),
        QStringLiteral("text/plain"),
        [context](const QString &uri) -> std::optional<QString> {
            const QString path = context.absolute(uri.mid(QStringLiteral("duet://diagram/").size()));
            if (DiagramView *view = context.tabs->diagramViewFor(path))
                return diagram::document::source(*view->board(), *view->codec());
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                return std::nullopt;
            return QString::fromUtf8(file.readAll());
        },
    });
}

} // namespace DuetTools
