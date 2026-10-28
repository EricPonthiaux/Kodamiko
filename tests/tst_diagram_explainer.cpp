// SPDX-License-Identifier: GPL-3.0-only

// The explainer board family (sidecar's free-board.js) and its JSON scene
// (fence/sciilo-board.js), with the tests of sidecar's free-board.test.js
// that make sense outside a browser.

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Canvas.h"
#include "viewers/diagram/Document.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/Graphviz.h"
#include "viewers/diagram/families/Families.h"
#include "viewers/diagram/families/explainer/Explainer.h"

#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QtTest>

#include <optional>

using namespace diagram;
using namespace diagram::explainer;

namespace {

const Codec &codec()
{
    const Codec *found = findCodec(QStringLiteral("explainer-board"));
    Q_ASSERT(found);
    return *found;
}

Shape *byKey(const Board &board, const QString &key)
{
    for (const auto &s : board.shapes()) {
        if (s->key == key)
            return s.get();
    }
    return nullptr;
}

QString scene(const QJsonArray &elements, const QJsonObject &extra = {})
{
    QJsonObject document{{QStringLiteral("format"), QStringLiteral("sciilo-board")},
                         {QStringLiteral("version"), 1},
                         {QStringLiteral("elements"), elements}};
    for (auto it = extra.begin(); it != extra.end(); ++it)
        document.insert(it.key(), it.value());
    return QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
}

QJsonObject element(const char *json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

// Parses and builds, as a document load does.
bool build(Board &board, const QString &source, QString *error = nullptr)
{
    Model model;
    if (!codec().parse(source, model, error))
        return false;
    buildBoard(board, codec(), model);
    return true;
}

QJsonObject reserialized(const Board &board)
{
    return QJsonDocument::fromJson(codec().serialize(board, board.meta).toUtf8()).object();
}

QJsonObject elementById(const QJsonObject &document, const QString &id)
{
    for (const QJsonValue &value : document.value(QStringLiteral("elements")).toArray()) {
        if (value.toObject().value(QStringLiteral("id")).toString() == id)
            return value.toObject();
    }
    return {};
}

QString parseError(const QJsonArray &elements)
{
    Model model;
    QString error;
    if (codec().parse(scene(elements), model, &error))
        return {};
    return error;
}

graphviz::ArrangeOptions familyOptions()
{
    const Family &family = families::explainer();
    graphviz::ArrangeOptions options;
    options.clusters = family.graphvizClusters;
    options.inferClusterMembership = family.inferClusterMembership;
    options.stackDisconnected = family.graphvizStackDisconnected;
    options.adaptiveDirection = family.graphvizAdaptiveDirection;
    options.splines = family.graphvizSplines;
    return options;
}

bool inside(const QRectF &outer, const QRectF &inner)
{
    return inner.left() >= outer.left() - 0.5 && inner.right() <= outer.right() + 0.5 &&
           inner.top() >= outer.top() - 0.5 && inner.bottom() <= outer.bottom() + 0.5;
}

const char *const kRich = R"({
  "format": "sciilo-board",
  "version": 1,
  "canvas": {"width": 900, "height": 600},
  "elements": [
    {"id": "area", "type": "frame", "x": 20, "y": 20, "width": 500, "height": 400, "title": "Vectors"},
    {"id": "query", "type": "node", "parent": "area", "shape": "ellipse", "x": 80, "y": 90, "width": 180, "height": 80, "label": "q", "description": "Query vector", "tone": "accent"},
    {"id": "circle", "type": "node", "shape": "circle", "x": 320, "y": 90, "width": 96, "height": 96, "label": "C"},
    {"id": "triangle", "type": "node", "shape": "triangle", "x": 450, "y": 90, "width": 124, "height": 108, "label": "T"},
    {"id": "hexagon", "type": "node", "shape": "hexagon", "x": 610, "y": 90, "width": 180, "height": 82, "label": "H"},
    {"id": "axis", "type": "stroke", "x": 100, "y": 300, "points": [[0, 0], [250, -120]], "arrow": "end"},
    {"id": "free-arrow", "type": "edge", "from": {"x": 120, "y": 340}, "to": "query", "label": "projection", "line": "dashed"}
  ],
  "steps": [{"id": 1, "title": "Compare", "narration": "Compare both directions."}]
}
)";

} // namespace

class TestDiagramExplainer : public QObject {
    Q_OBJECT

private slots:

    void registersTheFamily()
    {
        const Family &family = families::explainer();
        QCOMPARE(family.id, QStringLiteral("explainer"));
        QCOMPARE(family.title, QStringLiteral("Explainer board"));
        QVERIFY(findFamily(QStringLiteral("explainer")));
        QCOMPARE(codec().language(), QStringLiteral("board"));
        QCOMPARE(defaultCodec(family), &codec());
        QCOMPARE(document::extension(QStringLiteral("explainer")), QStringLiteral("board"));
        QVERIFY(codec().sourceOwnsLayout());
        QVERIFY(!codec().arrangeWithGraphviz());
        QVERIFY(family.alignmentGuides && family.reparentOnDrop && family.graphvizClusters);
        QVERIFY(family.graphvizStackDisconnected && family.routeLinksOnMove && family.inferClusterMembership);
        QVERIFY(family.nestedCreation);
    }

    // a new explainer is a valid empty semantic board
    void blankIsAnEmptyBoard()
    {
        QVERIFY(codec().matches(codec().blank()));
        Model model;
        QVERIFY(codec().parse(codec().blank(), model, nullptr));
        QVERIFY(model.nodes.isEmpty());
        QVERIFY(model.links.isEmpty());
        QVERIFY(model.meta.value(QStringLiteral("steps")).toArray().isEmpty());

        Board board;
        QString error;
        const std::optional<document::Loaded> loaded =
            document::load(board, QStringLiteral("new.board"), document::blank(QStringLiteral("explainer")), &error);
        QVERIFY(loaded);
        QCOMPARE(loaded->codec, &codec());
        QVERIFY(board.isEmpty());
        QVERIFY(!loaded->needsArrange);
        // An empty board is written exactly as sidecar writes its draft.
        QCOMPARE(codec().serialize(board, board.meta), codec().blank());

        Board empty;
        const std::optional<document::Loaded> loadedEmpty =
            document::load(empty, QStringLiteral("empty.board"), QByteArray(), &error);
        QVERIFY(loadedEmpty);
        QCOMPARE(loadedEmpty->codec, &codec());
    }

    // sciilo-board round-trips public elements and free-point edges
    void roundTripsElementsAndFreePointEdges()
    {
        Board board;
        QString error;
        QVERIFY2(build(board, QString::fromUtf8(kRich), &error), qPrintable(error));
        // Seven elements, plus the hidden anchor of the free-point edge.
        QCOMPARE(int(board.shapes().size()), 7);
        QCOMPARE(int(board.links().size()), 1);
        QCOMPARE(byKey(board, QStringLiteral("query"))->parentBoundary, QStringLiteral("area"));

        const QJsonObject serialized = reserialized(board);
        QCOMPARE(serialized.value(QStringLiteral("canvas")).toObject(),
                 (QJsonObject{{QStringLiteral("width"), 900}, {QStringLiteral("height"), 600}}));
        QCOMPARE(serialized.value(QStringLiteral("steps")).toArray(),
                 QJsonArray{(QJsonObject{{QStringLiteral("id"), 1},
                                         {QStringLiteral("title"), QStringLiteral("Compare")},
                                         {QStringLiteral("narration"), QStringLiteral("Compare both directions.")}})});
        const QJsonArray elements = serialized.value(QStringLiteral("elements")).toArray();
        QCOMPARE(elements.size(), 7); // hidden endpoint anchors do not leak into the source
        // The source's order is kept.
        QStringList ids;
        for (const QJsonValue &value : elements)
            ids.append(value.toObject().value(QStringLiteral("id")).toString());
        QCOMPARE(ids, (QStringList{"area", "query", "circle", "triangle", "hexagon", "axis", "free-arrow"}));
        for (const char *shape : {"circle", "triangle", "hexagon"})
            QCOMPARE(elementById(serialized, QString::fromLatin1(shape)).value(QStringLiteral("shape")).toString(),
                     QString::fromLatin1(shape));
        const QJsonObject edge = elementById(serialized, QStringLiteral("free-arrow"));
        QCOMPARE(edge.value(QStringLiteral("from")).toObject(),
                 (QJsonObject{{QStringLiteral("x"), 120}, {QStringLiteral("y"), 340}}));
        QCOMPARE(edge.value(QStringLiteral("to")).toString(), QStringLiteral("query"));
        QCOMPARE(edge.value(QStringLiteral("line")).toString(), QStringLiteral("dashed"));
        QCOMPARE(edge.value(QStringLiteral("label")).toString(), QStringLiteral("projection"));
        QVERIFY(!edge.contains(QStringLiteral("arrow")));
        const QJsonObject query = elementById(serialized, QStringLiteral("query"));
        QCOMPARE(query.value(QStringLiteral("parent")).toString(), QStringLiteral("area"));
        QCOMPARE(query.value(QStringLiteral("tone")).toString(), QStringLiteral("accent"));
        QCOMPARE(query.value(QStringLiteral("description")).toString(), QStringLiteral("Query vector"));
        QCOMPARE(query.value(QStringLiteral("width")).toDouble(), 180.0);
        const QJsonObject axis = elementById(serialized, QStringLiteral("axis"));
        QCOMPARE(axis.value(QStringLiteral("points")).toArray(), (QJsonArray{QJsonArray{0, 0}, QJsonArray{250, -120}}));
        QCOMPARE(axis.value(QStringLiteral("arrow")).toString(), QStringLiteral("end"));
    }

    // The text written is sidecar's JSON.stringify(document, null, 2), keys
    // in its order.
    void writesSidecarsLayout()
    {
        Board board;
        QVERIFY(
            build(board,
                  scene({element("{\"id\":\"a\",\"type\":\"node\",\"x\":1.234,\"y\":2,\"label\":\"A \\\"q\\\"\\n\"}"),
                         element(R"({"id":"s","type":"stroke","points":[[0,0],[10,0]],"label":"L","tone":"info"})")})));
        const QString text = codec().serialize(board, board.meta);
        QVERIFY2(text.contains(QStringLiteral("    {\n      \"id\": \"a\",\n      \"type\": \"node\",\n"
                                              "      \"shape\": \"rounded\",\n      \"x\": 1.23,\n      \"y\": 2,\n"
                                              "      \"width\": 180,\n      \"height\": 76,\n"
                                              "      \"label\": \"A \\\"q\\\"\\n\"\n    }")),
                 qPrintable(text));
        QVERIFY2(
            text.contains(QStringLiteral("      \"points\": [\n        [\n          0,\n          0\n        ],\n")),
            qPrintable(text));
        QVERIFY(text.indexOf(QStringLiteral("\"tone\": \"info\"")) < text.indexOf(QStringLiteral("\"label\": \"L\"")));
        QVERIFY(
            text.startsWith(QStringLiteral("{\n  \"format\": \"sciilo-board\",\n  \"version\": 1,\n  \"canvas\": {\n"
                                           "    \"width\": 1200,\n    \"height\": 800\n  },\n  \"elements\": [\n")));
    }

    // an unknown optional tone cannot make a saved board unreadable
    void unknownToneIsNormalized()
    {
        Model model;
        QVERIFY(codec().parse(
            scene({element(R"({"id":"title","type":"node","x":40,"y":30,"label":"Overview","tone":"primary"})")}),
            model, nullptr));
        QCOMPARE(model.nodes[0].props.value(QStringLiteral("tone")).toString(), QStringLiteral("neutral"));
        Board board;
        buildBoard(board, codec(), model);
        QVERIFY(!elementById(reserialized(board), QStringLiteral("title")).contains(QStringLiteral("tone")));
    }

    // agent-style animation values and object stroke points are normalized
    void agentValuesAreNormalized()
    {
        Model model;
        QVERIFY(codec().parse(
            scene(
                {element(R"({"id":"title","type":"node","x":40,"y":30,"label":"Overview","appearAt":0})"),
                 element(
                     R"({"id":"axis_x","type":"stroke","x":80,"y":160,"points":[{"x":0,"y":0},{"x":240,"y":0}],"arrow":"end"})"),
                 element(R"({"id":"later","type":"node","x":40,"y":300,"label":"Later","appearAt":3})")}),
            model, nullptr));
        QVERIFY(!model.nodes[0].props.contains(QStringLiteral("appearAt")));
        QCOMPARE(model.nodes[1].props.value(QStringLiteral("points")).toArray(),
                 (QJsonArray{QJsonArray{0, 0}, QJsonArray{240, 0}}));
        Board board;
        buildBoard(board, codec(), model);
        const QJsonObject serialized = reserialized(board);
        QVERIFY(!elementById(serialized, QStringLiteral("title")).contains(QStringLiteral("appearAt")));
        QCOMPARE(elementById(serialized, QStringLiteral("later")).value(QStringLiteral("appearAt")).toInt(), 3);
        QCOMPARE(elementById(serialized, QStringLiteral("axis_x")).value(QStringLiteral("points")).toArray(),
                 (QJsonArray{QJsonArray{0, 0}, QJsonArray{240, 0}}));
    }

    // the free creation palette exposes faithful common geometry
    void creationPalette()
    {
        const Family &family = families::explainer();
        QStringList ids;
        QStringList traced;
        for (const ElementPreset &preset : family.elements) {
            ids.append(preset.id);
            if (preset.trace) {
                traced.append(preset.id);
                // A line tool must not impose arbitrary endpoints.
                QVERIFY(!preset.props.contains(QStringLiteral("points")));
                QCOMPARE(preset.shapeType, QStringLiteral("freeStroke"));
            }
        }
        QCOMPARE(ids, (QStringList{"free-rounded", "free-rectangle", "free-circle", "free-ellipse", "free-triangle",
                                   "free-diamond", "free-hexagon", "free-text", "free-frame", "free-segment",
                                   "free-stroke"}));
        QCOMPARE(traced, (QStringList{"free-segment", "free-stroke"}));
        QCOMPARE(family.relations.size(), 2);
        QCOMPARE(family.relations[0].props.value(QStringLiteral("arrow")).toString(), QStringLiteral("end"));
        QCOMPARE(family.relations[1].props.value(QStringLiteral("arrow")).toString(), QStringLiteral("none"));

        // Every preset makes a shape that measures and draws.
        Board board;
        board.setFamily(&family);
        for (const ElementPreset &preset : family.elements) {
            QVERIFY(board.addShape(preset.shapeType, preset.props));
            QVERIFY(!Canvas::presetIcon(family, preset, board.style()).isNull());
        }
        for (const RelationPreset &preset : family.relations)
            QVERIFY(!Canvas::relationIcon(family, preset, board.style()).isNull());
        QImage image(400, 300, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        Canvas::paintBoard(painter, board);
    }

    // free shapes grow with text and retain that room until explicitly resized
    void shapesGrowWithTheirText()
    {
        const QString longLabel = QStringLiteral(
            "A rather long explanation that checks that the whole text stays inside its shape without ever "
            "overflowing it");
        Board board;
        board.setFamily(&families::explainer());
        for (const QString &geometry : shapeNames()) {
            auto *shape = static_cast<FreeNodeShape *>(
                board.addShape(QStringLiteral("freeNode"), {{QStringLiteral("x"), 100},
                                                            {QStringLiteral("y"), 80},
                                                            {QStringLiteral("shape"), geometry},
                                                            {QStringLiteral("label"), QStringLiteral("Short")}}));
            QVERIFY(shape);
            const qreal shortArea = shape->w * shape->h;

            shape->label = longLabel;
            board.measureShape(*shape);
            const QRectF rect = shape->textRect();
            QVERIFY2(shape->w * shape->h > shortArea, qPrintable(geometry));
            for (const QString &line : shape->lines())
                QVERIFY2(text::width(Font(14), line) <= rect.width() + 1e-6, qPrintable(geometry));
            QVERIFY2(shape->lines().size() * 20 <= rect.height() + 1e-6, qPrintable(geometry));

            const qreal longArea = shape->w * shape->h;
            shape->label = QStringLiteral("Brief");
            board.measureShape(*shape);
            QCOMPARE(shape->w * shape->h, longArea); // an existing enlargement is kept

            shape->resizeTo(1, 1, board);
            QVERIFY2(shape->w * shape->h < longArea, qPrintable(geometry));
            const QRectF resized = shape->textRect();
            for (const QString &line : shape->lines())
                QVERIFY(text::width(Font(14), line) <= resized.width() + 1e-6);
            QVERIFY(shape->lines().size() * 20 <= resized.height() + 1e-6);
        }
    }

    // free-shape connection handles exist only while the shape is selected
    void portsOnlyWhenSelected()
    {
        const FreeNodeShape shape;
        QVERIFY(!shape.showConnectionPorts());
        QVERIFY(!shape.showConnectionPortsOnHover());
    }

    // a free frame keeps its children attached and components can be enlarged
    void frameHoldsItsChildren()
    {
        Board board;
        board.setFamily(&families::explainer());
        Shape *frame =
            board.addShape(QStringLiteral("freeFrame"), {{QStringLiteral("key"), QStringLiteral("frame")},
                                                         {QStringLiteral("x"), 0},
                                                         {QStringLiteral("y"), 0},
                                                         {QStringLiteral("w"), 360},
                                                         {QStringLiteral("h"), 260},
                                                         {QStringLiteral("title"), QStringLiteral("Context")}});
        auto *child = static_cast<FreeNodeShape *>(
            board.addShape(QStringLiteral("freeNode"), {{QStringLiteral("parentBoundary"), QStringLiteral("frame")},
                                                        {QStringLiteral("x"), 40},
                                                        {QStringLiteral("y"), 80},
                                                        {QStringLiteral("w"), 140},
                                                        {QStringLiteral("h"), 70},
                                                        {QStringLiteral("label"), QStringLiteral("Component")}}));
        QVERIFY(frame->movesChildren());
        QCOMPARE(board.childShapes(frame), QList<Shape *>{child});

        child->x = -200;
        child->y = -200;
        board.containShape(*child);
        const QRectF content = *frame->childBounds(board);
        QCOMPARE(QPointF(child->x, child->y), content.topLeft()); // it stays within the content area

        // The parent frame grows when its component needs more room.
        child->resizeTo(child->w + 300, child->h + 40, board);
        board.containShape(*child, true);
        QVERIFY(frame->x + frame->w >= child->x + child->w + 12);
        // And cannot shrink under it.
        frame->resizeTo(1, 1, board);
        QVERIFY(frame->x + frame->w >= child->x + child->w + 12);
        QVERIFY(frame->y + frame->h >= child->y + child->h + 12);

        child->resizeTo(1, 1, board);
        QVERIFY(child->w >= child->minWidth());
        QVERIFY(child->h >= child->minHeight());

        // Selected from its border and title, not from its interior.
        QVERIFY(frame->hitTest(QPointF(frame->x + 30, frame->y + 10)));
        QVERIFY(!frame->hitTest(QPointF(frame->cx(), frame->cy())));
        QVERIFY(frame->hitTest(QPointF(frame->x + 4, frame->cy())));
    }

    // reorganizing a free board keeps every component inside its frame
    void layoutKeepsComponentsInTheirFrames()
    {
        Board board;
        QVERIFY(build(
            board,
            scene({element(R"({"id":"frame","type":"frame","x":0,"y":0,"width":360,"height":260,"title":"Context"})"),
                   element(R"({"id":"child","type":"node","x":40,"y":80,"width":180,"height":76,"label":"Element"})"),
                   element(R"({"id":"outside","type":"node","x":500,"y":80,"label":"Outside"})"),
                   element(R"({"id":"n1","type":"node","x":0,"y":320,"label":"Remark 1"})"),
                   element(R"({"id":"n2","type":"node","x":240,"y":320,"label":"Remark 2"})"),
                   element(R"({"id":"e","type":"edge","from":"outside","to":"n1"})")})));
        QString error;
        QVERIFY2(graphviz::autoArrange(board, familyOptions(), &error), qPrintable(error));
        const Shape *frame = byKey(board, QStringLiteral("frame"));
        const Shape *child = byKey(board, QStringLiteral("child"));
        // A component visibly placed in a frame recovers its missing parent.
        QCOMPARE(child->parentBoundary, QStringLiteral("frame"));
        QVERIFY(inside(*frame->childBounds(board), child->rect()));
        // Nothing overlaps but a frame and what it holds.
        for (const auto &a : board.shapes()) {
            for (const auto &b : board.shapes()) {
                if (a == b || a->type == QLatin1String("freeFrame") || b->type == QLatin1String("freeFrame"))
                    continue;
                QVERIFY2(!a->rect().intersects(b->rect()), qPrintable(a->key + QStringLiteral(" / ") + b->key));
            }
            if (a->type == QLatin1String("freeNode") && a.get() != child)
                QVERIFY2(!frame->rect().intersects(a->rect()), qPrintable(a->key));
        }
        // A frame leaves the layout no smaller than its floor.
        // An invalid size (none given) fails the comparisons.
        const QSizeF floor = frame->layoutMinimumSize().value_or(QSizeF());
        QVERIFY(floor.isValid());
        QVERIFY(frame->w >= floor.width());
        QVERIFY(frame->h >= floor.height());
    }

    // frames and free strokes reserve enough room for their labels
    void framesAndStrokesReserveRoomForLabels()
    {
        Board board;
        board.setFamily(&families::explainer());
        const auto *frame = static_cast<FreeFrameShape *>(board.addShape(
            QStringLiteral("freeFrame"), {{QStringLiteral("w"), 220},
                                          {QStringLiteral("h"), 140},
                                          {QStringLiteral("title"), QStringLiteral("Title ").repeated(100)}}));
        QVERIFY(frame->w > 220);
        for (const QString &line : frame->titleLines())
            QVERIFY(text::width(Font(14, 600), line) <= frame->w - 28);
        QVERIFY(frame->h >= frame->titleHeight() + 40);

        const auto *stroke = static_cast<FreeStrokeShape *>(board.addShape(
            QStringLiteral("freeStroke"), {{QStringLiteral("x"), 20},
                                           {QStringLiteral("y"), 30},
                                           {QStringLiteral("points"), QJsonArray{QJsonArray{0, 0}, QJsonArray{80, 0}}},
                                           {QStringLiteral("label"), QStringLiteral("A very detailed label")}}));
        QVERIFY(stroke->strokeBounds().width() > stroke->w); // the bounds include the label
        QVERIFY(stroke->hitTest(QPointF(60, 31)));
        QVERIFY(!stroke->hitTest(QPointF(60, 60)));
    }

    // visual-explanation arrow shafts stop at the base of their heads
    void arrowShaftsStopAtTheirHeads()
    {
        const qreal inset = 11 * std::cos(graphvizNormalArrowSpread);
        Board board;
        board.setFamily(&families::explainer());
        const auto *stroke = static_cast<FreeStrokeShape *>(board.addShape(
            QStringLiteral("freeStroke"), {{QStringLiteral("x"), 10},
                                           {QStringLiteral("y"), 20},
                                           {QStringLiteral("points"), QJsonArray{QJsonArray{0, 0}, QJsonArray{100, 0}}},
                                           {QStringLiteral("arrow"), QStringLiteral("both")}}));
        const std::vector<QPointF> shaft = stroke->shaft();
        QCOMPARE(shaft.front().x(), 10 + inset);
        QCOMPARE(shaft.back().x(), 110 - inset);
        QCOMPARE(shaft.front().y(), 20.0);

        // A blocked edge stays straight until Graphviz has routed it.
        const Shape *from = board.addShape(QStringLiteral("freeNode"), {{QStringLiteral("x"), 0},
                                                                        {QStringLiteral("y"), 200},
                                                                        {QStringLiteral("w"), 40},
                                                                        {QStringLiteral("h"), 40}});
        board.addShape(QStringLiteral("freeNode"), {{QStringLiteral("x"), 130},
                                                    {QStringLiteral("y"), 180},
                                                    {QStringLiteral("w"), 80},
                                                    {QStringLiteral("h"), 80}});
        const Shape *to = board.addShape(QStringLiteral("freeNode"), {{QStringLiteral("x"), 300},
                                                                      {QStringLiteral("y"), 200},
                                                                      {QStringLiteral("w"), 40},
                                                                      {QStringLiteral("h"), 40}});
        const Link *edge = board.addLink(QStringLiteral("freeEdge"), from->id, to->id, {});
        QVERIFY(edge);
        const std::optional<Route> straight = edge->route(board);
        QVERIFY(straight && straight->curve.empty() && straight->points.empty());
        QCOMPARE(straight->a, from->borderPoint(QPointF(to->cx(), to->cy())));
        QCOMPARE(straight->b, to->borderPoint(QPointF(from->cx(), from->cy())));
        // Several edges may join the same shapes.
        QVERIFY(board.addLink(QStringLiteral("freeEdge"), from->id, to->id, {}) != edge);
    }

    // the semantic board rejects ambiguous or unsafe scene data
    void rejectsInvalidScenes()
    {
        QVERIFY(parseError({element(R"({"id":"same","type":"node","x":0,"y":0,"label":"A"})"),
                            element(R"({"id":"same","type":"node","x":10,"y":10,"label":"B"})")})
                    .contains(QStringLiteral("duplicate element id")));
        QVERIFY(parseError({element(R"({"id":"a","type":"node","x":0,"y":0,"label":"A"})"),
                            element(R"({"id":"bad-link","type":"edge","from":"a","to":"missing"})")})
                    .contains(QStringLiteral("unknown element")));
        QVERIFY(
            parseError(
                {element(
                    "{\"id\":\"a\",\"type\":\"node\",\"x\":0,\"y\":0,\"label\":\"A\",\"fill\":\"url(javascript:1)\"}")})
                .contains(QStringLiteral("fill must be")));
        QCOMPARE(parseError({element(R"({"id":"a","type":"blob"})")}),
                 QStringLiteral("Invalid sciilo-board: unsupported element type \"blob\""));
        QCOMPARE(parseError({element(R"({"id":"__a","type":"node","x":0,"y":0})")}),
                 QStringLiteral("Invalid sciilo-board: invalid element id \"__a\""));
        QCOMPARE(parseError({element(R"({"id":"a","type":"node","x":0,"y":0,"shape":"star"})")}),
                 QStringLiteral("Invalid sciilo-board: a.shape is unsupported"));
        QCOMPARE(parseError({element(R"({"id":"a","type":"node","x":0,"y":0,"width":5})")}),
                 QStringLiteral("Invalid sciilo-board: a.width is out of range"));
        QCOMPARE(parseError({element(R"({"id":"a","type":"node","x":0})")}),
                 QStringLiteral("Invalid sciilo-board: a.y is out of range"));
        QCOMPARE(parseError({element(R"({"id":"s","type":"stroke","points":[[0,0]]})")}),
                 QStringLiteral("Invalid sciilo-board: s.points must contain 2 to 512 points"));
        QCOMPARE(parseError({element(R"({"id":"s","type":"stroke","points":[[0,0],"x"]})")}),
                 QStringLiteral("Invalid sciilo-board: s.points[1] must be [x, y] or {x, y}"));
        QCOMPARE(parseError({element(R"({"id":"a","type":"node","x":0,"y":0})"),
                             element(R"({"id":"b","type":"node","x":0,"y":0,"parent":"a"})")}),
                 QStringLiteral("Invalid sciilo-board: b.parent must reference another frame"));
        QCOMPARE(parseError({element(R"({"id":"f","type":"frame","x":0,"y":0,"parent":"g"})"),
                             element(R"({"id":"g","type":"frame","x":0,"y":0,"parent":"f"})")}),
                 QStringLiteral("Invalid sciilo-board: f.parent creates a frame cycle"));
        QCOMPARE(parseError({element(R"({"id":"e","type":"edge","to":"x"})")}),
                 QStringLiteral("Invalid sciilo-board: e.from must be an object"));

        Model model;
        QString error;
        QVERIFY(!codec().parse(QStringLiteral("{\"format\": \"sciilo-board\", "), model, &error));
        QCOMPARE(error, QStringLiteral("Invalid sciilo-board: source is not valid JSON"));
        QVERIFY(!codec().parse(QStringLiteral("{\"format\": \"sciilo-board\", \"version\": 2, \"elements\": []}"),
                               model, &error));
        QCOMPARE(error, QStringLiteral("Invalid sciilo-board: version must be 1"));
        QVERIFY(!codec().parse(QStringLiteral("{\"format\": \"sciilo-board\", \"version\": 1}"), model, &error));
        QCOMPARE(error, QStringLiteral("Invalid sciilo-board: elements must be an array"));

        // The document refuses it with that message.
        Board board;
        QVERIFY(!document::load(
            board, QStringLiteral("bad.board"),
            scene({element(R"({"id":"a","type":"node","x":0,"y":0,"tone":"x","fill":"red"})")}).toUtf8(), &error));
        QCOMPARE(error, QStringLiteral("Invalid sciilo-board: a.fill must be a six-digit hex color"));
    }

    // Graphviz routes against the geometry the board actually draws
    void graphvizGeometry()
    {
        Board board;
        board.setFamily(&families::explainer());
        const QHash<QString, QString> expected{
            {"rectangle", "box"},   {"rounded", "box"},       {"text", "box"},        {"circle", "circle"},
            {"ellipse", "ellipse"}, {"triangle", "triangle"}, {"diamond", "diamond"}, {"hexagon", "hexagon"},
        };
        for (auto it = expected.begin(); it != expected.end(); ++it) {
            const Shape *node =
                board.addShape(QStringLiteral("freeNode"),
                               {{QStringLiteral("shape"), it.key()}, {QStringLiteral("label"), QStringLiteral("A")}});
            QVERIFY2(node->graphvizNodeAttributes(board, QStringLiteral("TB"), node->w, node->h)
                         .contains(QStringLiteral("shape=") + it.value()),
                     qPrintable(it.key()));
        }
        // Graphviz fills only the top three quarters of a node box with its
        // triangle: the box reserved for one is a third taller than drawn.
        const Shape *triangle =
            board.addShape(QStringLiteral("freeNode"), {{QStringLiteral("shape"), QStringLiteral("triangle")},
                                                        {QStringLiteral("w"), 124},
                                                        {QStringLiteral("h"), 108},
                                                        {QStringLiteral("label"), QStringLiteral("Key")}});
        QCOMPARE(triangle->graphvizLayoutSize().value_or(QSizeF()), QSizeF(triangle->w, triangle->h / 0.75));
        QVERIFY(!board.addShape(QStringLiteral("freeNode"), {{QStringLiteral("shape"), QStringLiteral("circle")}})
                     ->graphvizLayoutSize());

        // A line without an arrow reaches the shape it points at: the board
        // draws its own heads.
        FreeEdge edge;
        for (const char *arrow : {"none", "end", "start", "both"}) {
            edge.arrow = QString::fromLatin1(arrow);
            QCOMPARE(edge.graphvizAttributes(board), QStringList{QStringLiteral("dir=none")});
        }

        // Border points lie on the outline drawn.
        const auto *circle = static_cast<FreeNodeShape *>(board.addShape(
            QStringLiteral("freeNode"),
            {{QStringLiteral("shape"), QStringLiteral("circle")}, {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}}));
        const QPointF p = circle->borderPoint(QPointF(500, 500));
        QCOMPARE(std::hypot(p.x() - circle->cx(), p.y() - circle->cy()), circle->w / 2);
        const auto *diamond = static_cast<FreeNodeShape *>(
            board.addShape(QStringLiteral("freeNode"), {{QStringLiteral("shape"), QStringLiteral("diamond")},
                                                        {QStringLiteral("x"), 0},
                                                        {QStringLiteral("y"), 0}}));
        QCOMPARE(diamond->borderPoint(QPointF(1000, diamond->cy())), QPointF(diamond->w, diamond->cy()));
    }

    // deleting a frame leaves no child pointing at a shape that is gone, and
    // a nested frame deleted alone hands its content to the frame above
    void deletingAFrameHandsItsContentOver()
    {
        Board board;
        QVERIFY(build(
            board,
            scene(
                {element(R"({"id":"outer","type":"frame","x":0,"y":0,"width":600,"height":460,"title":"System"})"),
                 element(
                     R"({"id":"inner","type":"frame","parent":"outer","x":40,"y":80,"width":360,"height":260,"title":"Subsystem"})"),
                 element(R"({"id":"child","type":"node","parent":"inner","x":60,"y":140,"label":"Component"})")})));
        QVERIFY(board.removeShape(byKey(board, QStringLiteral("inner"))->id));
        QCOMPARE(byKey(board, QStringLiteral("child"))->parentBoundary, QStringLiteral("outer"));
        QString error;
        Board again;
        QVERIFY2(build(again, codec().serialize(board, board.meta), &error), qPrintable(error));

        QVERIFY(board.removeShape(byKey(board, QStringLiteral("outer"))->id));
        QVERIFY(byKey(board, QStringLiteral("child"))->parentBoundary.isEmpty());
        QVERIFY(!elementById(reserialized(board), QStringLiteral("child")).contains(QStringLiteral("parent")));
        QVERIFY2(build(again, codec().serialize(board, board.meta), &error), qPrintable(error));
    }

    // a pasted copy keeps its own identity instead of erasing the original
    void pastedCopyKeepsItsIdentity()
    {
        Board board;
        QVERIFY(build(
            board,
            scene({element(R"({"id":"frame","type":"frame","x":0,"y":0,"width":360,"height":260,"title":"Context"})"),
                   element(R"({"id":"child","type":"node","parent":"frame","x":40,"y":80,"label":"Component"})")})));
        board.selectAll();
        QVERIFY(board.copySelection());
        QVERIFY(board.pasteClipboard());
        QCOMPARE(int(board.shapes().size()), 4);
        QSet<QString> keys;
        for (const auto &s : board.shapes())
            keys.insert(s->key);
        QCOMPARE(keys.size(), 4);
        QCOMPARE(byKey(board, QStringLiteral("child_copy"))->parentBoundary, QStringLiteral("frame_copy"));
        QCOMPARE(byKey(board, QStringLiteral("child"))->parentBoundary, QStringLiteral("frame"));

        const QJsonObject serialized = reserialized(board);
        QCOMPARE(serialized.value(QStringLiteral("elements")).toArray().size(), 4); // the copy survives the save
        QCOMPARE(elementById(serialized, QStringLiteral("child_copy")).value(QStringLiteral("parent")).toString(),
                 QStringLiteral("frame_copy"));
        Board again;
        QString error;
        QVERIFY2(build(again, codec().serialize(board, board.meta), &error), qPrintable(error));
    }

    // Shapes created in Kodamiko have no key: their board ids become element ids,
    // unique even when one collides with a source id.
    void createdElementsGetUniqueIds()
    {
        // Source ids that look like board ids ("s1", "l1"): the worst case.
        QJsonArray elements;
        for (int i = 1; i <= 6; ++i)
            elements.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("s%1").arg(i)},
                                        {QStringLiteral("type"), QStringLiteral("node")},
                                        {QStringLiteral("x"), i * 200},
                                        {QStringLiteral("y"), 0}});
        elements.append(element(R"({"id":"l1","type":"edge","from":"s1","to":"s2"})"));
        Board board;
        QVERIFY(build(board, scene(elements)));
        QStringList created;
        for (int i = 0; i < 4; ++i)
            created.append(
                board
                    .addShape(QStringLiteral("freeNode"), {{QStringLiteral("x"), i * 200}, {QStringLiteral("y"), 300}})
                    ->id);
        QVERIFY(board.connect(created[0], created[1]));
        QVERIFY(board.connect(byKey(board, QStringLiteral("s1"))->id, created[2]));
        QString error;
        Board again;
        QVERIFY2(build(again, codec().serialize(board, board.meta), &error), qPrintable(error));
        QCOMPARE(int(again.shapes().size()), 10);
        QCOMPARE(int(again.links().size()), 3);
        QVERIFY(byKey(again, QStringLiteral("s1")));
    }

    // document::save → document::load keeps everything.
    void documentRoundTrip()
    {
        Board board;
        QString error;
        const std::optional<document::Loaded> loaded =
            document::load(board, QStringLiteral("rich.board"), QByteArray(kRich), &error);
        QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->codec, &codec());
        QVERIFY(!loaded->needsArrange);
        Shape *query = byKey(board, QStringLiteral("query"));
        query->x += 15;
        auto *node = static_cast<FreeNodeShape *>(query);
        node->label = QStringLiteral("q'");
        node->customFill = QStringLiteral("#ffcc00");
        const QByteArray saved = document::save(board, codec());
        QVERIFY(!saved.contains("kodamiko")); // the scene owns its layout: no comments
        QVERIFY(saved.endsWith('\n'));

        Board reloaded;
        QVERIFY2(document::load(reloaded, QStringLiteral("rich.board"), saved, &error), qPrintable(error));
        QCOMPARE(int(reloaded.shapes().size()), int(board.shapes().size()));
        QCOMPARE(int(reloaded.links().size()), 1);
        for (const auto &s : board.shapes()) {
            const Shape *other = byKey(reloaded, s->key);
            QVERIFY2(other, qPrintable(s->key));
            QCOMPARE(other->type, s->type);
            QCOMPARE(other->rect(), s->rect());
            QCOMPARE(other->parentBoundary, s->parentBoundary);
            QCOMPARE(other->customFill, s->customFill);
            QCOMPARE(other->displayText(), s->displayText());
        }
        QCOMPARE(document::save(reloaded, codec()), saved);
        QCOMPARE(reloaded.links().front()->label, QStringLiteral("projection"));
        QCOMPARE(reloaded.links().front()->customLineStyle, QStringLiteral("dashed"));
    }

    // Elements written without a place (by hand, by Claude) are laid out;
    // the others never move.
    void elementsWithoutCoordinatesAreLaidOut()
    {
        const QByteArray source = scene({element(R"({"id":"a","type":"node","label":"First"})"),
                                         element(R"({"id":"b","type":"node","label":"Second","shape":"circle"})"),
                                         element(R"({"id":"c","type":"node","label":"Third","shape":"diamond"})"),
                                         element(R"({"id":"f","type":"frame","title":"Group"})"),
                                         element(R"({"id":"d","type":"node","parent":"f","label":"Inside"})"),
                                         element(R"({"id":"ab","type":"edge","from":"a","to":"b","label":"then"})"),
                                         element(R"({"id":"bc","type":"edge","from":"b","to":"c"})")})
                                      .toUtf8();
        Board board;
        QString error;
        const std::optional<document::Loaded> loaded =
            document::load(board, QStringLiteral("agent.board"), source, &error);
        QVERIFY2(loaded, qPrintable(error));
        QVERIFY(loaded->needsArrange);
        QVERIFY2(graphviz::autoArrange(board, familyOptions(), &error), qPrintable(error));
        for (const auto &a : board.shapes()) {
            for (const auto &b : board.shapes()) {
                if (a != b && a->type != QLatin1String("freeFrame") && b->type != QLatin1String("freeFrame"))
                    QVERIFY2(!a->rect().intersects(b->rect()), qPrintable(a->key + QStringLiteral(" / ") + b->key));
            }
        }
        const Shape *frame = byKey(board, QStringLiteral("f"));
        QVERIFY(inside(*frame->childBounds(board), byKey(board, QStringLiteral("d"))->rect()));
        // Once saved, the places are source data.
        Board reloaded;
        const std::optional<document::Loaded> reloadedResult =
            document::load(reloaded, QStringLiteral("agent.board"), document::save(board, codec()), &error);
        QVERIFY(reloadedResult);
        QVERIFY(!reloadedResult->needsArrange);
        QCOMPARE(byKey(reloaded, QStringLiteral("c"))->rect(), byKey(board, QStringLiteral("c"))->rect());
    }

    void drawsWithoutCrashing()
    {
        Board board;
        QVERIFY(build(board, QString::fromUtf8(kRich)));
        board.addShape(QStringLiteral("freeNode"), {{QStringLiteral("shape"), QStringLiteral("text")},
                                                    {QStringLiteral("label"), QStringLiteral("Note")}});
        board.selectAll();
        QVERIFY(graphviz::routeLinks(board));
        QImage image(1000, 700, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        Canvas::paintBoard(painter, board);
        painter.end();
        QVERIFY(!board.contentBounds().isEmpty());
    }
};

QTEST_MAIN(TestDiagramExplainer)
#include "tst_diagram_explainer.moc"
