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

#include <optional>

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

// How Claude names an element: its key in the source, else its id.
QString keyOf(const diagram::Shape &shape)
{
    return shape.key.isEmpty() ? shape.id : shape.key;
}

QJsonObject elementJson(const diagram::Board &board, const diagram::Shape &shape)
{
    QJsonObject element{{QStringLiteral("key"), keyOf(shape)},         {QStringLiteral("type"), shape.type},
                        {QStringLiteral("text"), shape.displayText()}, {QStringLiteral("x"), qRound(shape.x)},
                        {QStringLiteral("y"), qRound(shape.y)},        {QStringLiteral("width"), qRound(shape.w)},
                        {QStringLiteral("height"), qRound(shape.h)}};
    if (!shape.parentBoundary.isEmpty())
        element.insert(QStringLiteral("parent"), shape.parentBoundary);
    if (board.selection().contains(shape.id))
        element.insert(QStringLiteral("selected"), true);
    return element;
}

QJsonObject linkJson(const diagram::Board &board, const diagram::Link &link)
{
    const auto endKey = [&board](const QString &id) {
        const diagram::Shape *shape = board.shape(id);
        return shape ? keyOf(*shape) : id;
    };
    QJsonObject json{{QStringLiteral("from"), endKey(link.from)},
                     {QStringLiteral("to"), endKey(link.to)},
                     {QStringLiteral("type"), link.type},
                     {QStringLiteral("label"), link.label}};
    if (board.linkSelection().contains(link.id))
        json.insert(QStringLiteral("selected"), true);
    return json;
}

QJsonObject boardJson(const diagram::Board &board, const diagram::Codec &codec)
{
    QJsonArray elements;
    for (const auto &shape : board.shapes())
        elements.append(elementJson(board, *shape));
    QJsonArray links;
    for (const auto &link : board.links())
        links.append(linkJson(board, *link));
    return QJsonObject{{QStringLiteral("family"), board.family() ? board.family()->id : QString()},
                       {QStringLiteral("notation"), codec.language()},
                       {QStringLiteral("source"), diagram::document::source(board, codec)},
                       {QStringLiteral("elements"), elements},
                       {QStringLiteral("links"), links}};
}

QString pathOf(const DuetToolContext &context, const QJsonObject &arguments)
{
    return context.absolute(arguments.value(QStringLiteral("path")).toString());
}

McpResult listDiagramKinds(const DuetToolContext &, const QJsonObject &)
{
    QJsonArray kinds;
    for (const DiagramKind &kind : DiagramKinds::available())
        kinds.append(QJsonObject{{QStringLiteral("id"), kind.id},
                                 {QStringLiteral("title"), kind.title},
                                 {QStringLiteral("extension"), kind.extension},
                                 {QStringLiteral("blank"), QString::fromUtf8(kind.blank)}});
    return McpResult::json(QJsonObject{{QStringLiteral("kinds"), kinds}});
}

McpResult getDiagram(const DuetToolContext &context, const QJsonObject &arguments)
{
    const QString path = pathOf(context, arguments);
    if (const DiagramView *view = context.tabs->diagramViewFor(path)) {
        QJsonObject json = boardJson(*view->board(), *view->codec());
        json.insert(QStringLiteral("open"), true);
        json.insert(QStringLiteral("unsaved"), view->isModified());
        return McpResult::json(json);
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return McpResult::error(QStringLiteral("Cannot read %1").arg(context.relative(path)));
    diagram::Board board;
    QString error;
    const std::optional<diagram::document::Loaded> loaded =
        diagram::document::load(board, path, file.readAll(), &error);
    if (!loaded)
        return McpResult::error(error);
    QJsonObject json = boardJson(board, *loaded->codec);
    json.insert(QStringLiteral("open"), false);
    return McpResult::json(json);
}

McpResult getDiagramProblems(const DuetToolContext &context, const QJsonObject &arguments)
{
    const QString path = pathOf(context, arguments);
    QByteArray content;
    if (const DiagramView *view = context.tabs->diagramViewFor(path)) {
        content = view->content();
    } else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return McpResult::error(QStringLiteral("Cannot read %1").arg(context.relative(path)));
        content = file.readAll();
    }
    return McpResult::json(
        QJsonObject{{QStringLiteral("problems"), QJsonArray::fromStringList(DiagramCheck::problems(path, content))}});
}

McpResult applyDiagramSource(const DuetToolContext &context, const QJsonObject &arguments)
{
    QString error;
    const DiagramView *view = openDiagram(context, pathOf(context, arguments), &error);
    if (!view)
        return McpResult::error(error);
    const QString source = arguments.value(QStringLiteral("source")).toString();
    const std::optional<diagram::document::Loaded> loaded =
        diagram::document::applySource(*view->board(), *view->codec(), source, &error);
    if (!loaded)
        return McpResult::error(error);
    view->canvas()->settle(*loaded, true);
    QJsonObject json{{QStringLiteral("elements"), int(view->board()->shapes().size())},
                     {QStringLiteral("links"), int(view->board()->links().size())}};
    if (!loaded->warnings.isEmpty())
        json.insert(QStringLiteral("problems"), QJsonArray::fromStringList(loaded->warnings));
    return McpResult::json(json);
}

McpResult layoutDiagram(const DuetToolContext &context, const QJsonObject &arguments)
{
    QString error;
    const DiagramView *view = openDiagram(context, pathOf(context, arguments), &error);
    if (!view)
        return McpResult::error(error);
    if (arguments.contains(QStringLiteral("direction")))
        view->board()->meta.insert(QStringLiteral("rankdir"), arguments.value(QStringLiteral("direction")).toString());
    if (!view->canvas()->arrange(true))
        return McpResult::error(QStringLiteral("The layout failed"));
    return McpResult::message(QStringLiteral("Laid out"));
}

McpResult selectDiagramElements(const DuetToolContext &context, const QJsonObject &arguments)
{
    QString error;
    const DiagramView *view = openDiagram(context, pathOf(context, arguments), &error);
    if (!view)
        return McpResult::error(error);
    diagram::Board *board = view->board();
    board->clearSelection();
    QStringList missing;
    for (const QJsonValue &key : arguments.value(QStringLiteral("keys")).toArray()) {
        if (const diagram::Shape *shape = board->shapeByReference(key.toString()))
            board->selectShape(shape->id, true);
        else
            missing.append(key.toString());
    }
    return missing.isEmpty()
               ? McpResult::message(QStringLiteral("Selected"))
               : McpResult::error(QStringLiteral("No element %1").arg(missing.join(QStringLiteral(", "))));
}

McpResult exportDiagram(const DuetToolContext &context, const QJsonObject &arguments)
{
    const QString output = context.absolute(arguments.value(QStringLiteral("output")).toString());
    QString error;
    const DiagramView *view = openDiagram(context, pathOf(context, arguments), &error);
    if (!view)
        return McpResult::error(error);
    if (!view->exportImage(output, &error))
        return McpResult::error(error);
    return McpResult::message(QStringLiteral("Wrote %1").arg(context.relative(output)));
}

QString drawDiagramPrompt(const QJsonObject &arguments)
{
    const QString kind = arguments.value(QStringLiteral("kind")).toString();
    return QStringLiteral("Following the duet:diagrams skill, draw %1 of %2: read the code it "
                          "refers to first and draw what it really does. Then open it in Duet "
                          "(open_file) and check it reads cleanly (get_diagram_problems).")
        .arg(kind.isEmpty() ? QStringLiteral("the most fitting diagram") : QStringLiteral("a %1 diagram").arg(kind),
             arguments.value(QStringLiteral("subject")).toString());
}

std::optional<QString> readDiagramResource(const DuetToolContext &context, const QString &uri)
{
    const QString path = context.absolute(uri.mid(QStringLiteral("duet://diagram/").size()));
    if (const DiagramView *view = context.tabs->diagramViewFor(path))
        return diagram::document::source(*view->board(), *view->codec());
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return QString::fromUtf8(file.readAll());
}

} // namespace

void registerDiagramTools(McpRegistry &registry, const DuetToolContext &context)
{
    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("list_diagram_kinds"),
         .title = QStringLiteral("Diagram kinds"),
         .description = QStringLiteral("The kinds of diagram Duet's editor draws, with the file extension and a blank "
                                       "source of each (follow the duet:diagrams skill to write one)."),
         .inputSchema = object({})},
        handledBy(context, listDiagramKinds)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_diagram"),
         .title = QStringLiteral("Diagram"),
         .description = QStringLiteral(
             "A diagram as Duet's editor has it: its kind, its source (with unsaved changes), its elements with their "
             "place and size, its links, and what the user has selected in it. Use it when the user talks about "
             "\"this element\" or \"the selected ones\" of a diagram."),
         .inputSchema = object({{QStringLiteral("path"), pathArgument()}}, {QStringLiteral("path")})},
        handledBy(context, getDiagram)));

    registry.addTool(McpTool::reading(
        {.name = QStringLiteral("get_diagram_problems"),
         .title = QStringLiteral("Diagram problems"),
         .description = QStringLiteral("What Duet's diagram editor cannot read in a diagram file (an unsupported "
                                       "kind, statements it does not understand): empty when the diagram reads as "
                                       "written."),
         .inputSchema = object({{QStringLiteral("path"), pathArgument()}}, {QStringLiteral("path")})},
        handledBy(context, getDiagramProblems)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("apply_diagram_source"),
         .title = QStringLiteral("Change a diagram"),
         .description = QStringLiteral(
             "Replaces an open diagram's content with a new source in the same notation, in Duet's editor (one step "
             "the user can undo; elements keep their place by id, new ones are placed beside their neighbors). The "
             "file is not written: the user saves. Use it to change a diagram the user is looking at; to create one, "
             "write the file."),
         .inputSchema = object({{QStringLiteral("path"), pathArgument()},
                                {QStringLiteral("source"), string(QStringLiteral("The whole new source, without the "
                                                                                 "\"duet\" layout comments"))}},
                               {QStringLiteral("path"), QStringLiteral("source")})},
        handledBy(context, applyDiagramSource)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("layout_diagram"),
         .title = QStringLiteral("Lay a diagram out"),
         .description = QStringLiteral("Lays an open diagram out again automatically in Duet's editor (the user's "
                                       "arrangement is replaced; one step they can undo)."),
         .inputSchema = object(
             {{QStringLiteral("path"), pathArgument()},
              {QStringLiteral("direction"),
               enumeration({QStringLiteral("TB"), QStringLiteral("LR"), QStringLiteral("BT"), QStringLiteral("RL")},
                           QStringLiteral("Reading direction, for the kinds that have one (optional)"))}},
             {QStringLiteral("path")})},
        handledBy(context, layoutDiagram)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("select_diagram_elements"),
         .title = QStringLiteral("Select in a diagram"),
         .description = QStringLiteral("Selects elements of an open diagram by key (their source ids), to show them "
                                       "to the user; an empty list clears the selection."),
         .inputSchema = object({{QStringLiteral("path"), pathArgument()},
                                {QStringLiteral("keys"),
                                 array(string(QStringLiteral("Element key")), QStringLiteral("Elements to select"))}},
                               {QStringLiteral("path"), QStringLiteral("keys")})},
        handledBy(context, selectDiagramElements)));

    registry.addTool(McpTool::acting(
        {.name = QStringLiteral("export_diagram"),
         .title = QStringLiteral("Export a diagram"),
         .description = QStringLiteral("Writes a diagram as an image, SVG or PNG by the output's extension, in a "
                                       "light printable palette (for documentation)."),
         .inputSchema =
             object({{QStringLiteral("path"), pathArgument()},
                     {QStringLiteral("output"), string(QStringLiteral("Image file to write (.svg or .png)"))}},
                    {QStringLiteral("path"), QStringLiteral("output")})},
        handledBy(context, exportDiagram)));

    registry.addPrompt(
        {QStringLiteral("draw-diagram"),
         QStringLiteral("Draw a diagram"),
         QStringLiteral("Draw a diagram of part of the project, opened in Duet's diagram editor"),
         {{QStringLiteral("subject"), QStringLiteral("What it shows (\"the login flow in src/auth\")"), true},
          {QStringLiteral("kind"),
           QStringLiteral("Kind: flowchart, sequence, class, state, data model, "
                          "C4, mind map… (optional)"),
           false}},
         drawDiagramPrompt});

    registry.addResource({
        QStringLiteral("duet://diagram/{path}"),
        true,
        QStringLiteral("diagram"),
        QStringLiteral("Diagram as in the editor"),
        QStringLiteral("A diagram's source with its unsaved changes"),
        QStringLiteral("text/plain"),
        [context](const QString &uri) { return readDiagramResource(context, uri); },
    });
}

} // namespace DuetTools
