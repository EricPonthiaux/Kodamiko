// SPDX-License-Identifier: GPL-3.0-only

// The state diagram family (sidecar's state-diagram.js and its Mermaid and
// PlantUML fences): parsing, serialization round trips, measures, label
// resolution, containment and layout.

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Canvas.h"
#include "viewers/diagram/Document.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/Graphviz.h"
#include "viewers/diagram/families/Families.h"
#include "viewers/diagram/families/states/States.h"

#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QtTest>

#include <optional>

using namespace diagram;
using namespace diagram::states;

namespace {

const Codec &mermaid()
{
    return *families::stateCodecs().at(0);
}

const Codec &plantuml()
{
    return *families::stateCodecs().at(1);
}

const char *const kNestedMermaid = R"(stateDiagram-v2
  Active: Operational
  state Active {
    [*] --> Idle
    state Nested {
      [*] --> Ready
      Ready --> [*]
    }
    Idle --> Nested
  }
  [*] --> Active
  Active --> [*])";

const char *const kNestedPlantuml = R"(@startuml
state "Operational" as Active
state Active {
  [*] --> Idle
  state Nested {
    [*] --> Ready
    Ready --> [*]
  }
  Idle --> Nested
}
[*] --> Active
Active --> [*]
@enduml)";

const char *const kRichMermaid = R"(stateDiagram-v2
  direction LR
  state "Waiting for payment" as Waiting
  Waiting : order placed
  Waiting : reminder after 24 h
  state check <<choice>>
  state split <<fork>>
  state merge <<join>>
  [*] --> Waiting
  Waiting --> Waiting : retry
  Waiting --> Waiting : wait\nthen retry
  Waiting --> check : paid
  check --> split : in stock
  check --> Cancelled : out of stock
  split --> Packing
  split --> Invoicing
  Packing --> merge
  Invoicing --> merge
  state Shipping {
    [*] --> Packed
    Packed --> Sent : carrier picks up
    Sent --> [*]
  }
  merge --> Shipping
  Shipping --> [*]
  Cancelled --> [*])";

const Model::Node *nodeBy(const Model &model, const QString &field, const QString &value)
{
    for (const Model::Node &node : model.nodes) {
        if (node.props.value(field).toString() == value)
            return &node;
    }
    return nullptr;
}

int countType(const Model &model, const QString &type)
{
    int count = 0;
    for (const Model::Node &node : model.nodes)
        count += node.type == type;
    return count;
}

Shape *byKey(const Board &board, const QString &key)
{
    for (const auto &s : board.shapes()) {
        if (s->key == key)
            return s.get();
    }
    return nullptr;
}

Model parsed(const Codec &codec, const QString &source)
{
    Model model;
    QString error;
    if (!codec.parse(source, model, &error))
        qWarning() << error;
    return model;
}

void build(Board &board, const Codec &codec, const QString &source)
{
    board.setStyle(Style::printed());
    buildBoard(board, codec, parsed(codec, source));
}

bool load(Board &board, const QString &name, const QByteArray &content, document::Loaded *loaded = nullptr)
{
    board.setStyle(Style::printed());
    QString error;
    std::optional<document::Loaded> result = document::load(board, name, content, &error);
    if (!result) {
        qWarning() << error;
        return false;
    }
    if (loaded)
        *loaded = std::move(*result);
    return true;
}

graphviz::ArrangeOptions familyOptions(const Board &board)
{
    graphviz::ArrangeOptions options;
    options.rankdir = board.meta.value(QStringLiteral("rankdir")).toString(QStringLiteral("TB"));
    options.clusters = families::states().graphvizClusters;
    return options;
}

// Whether `inner` lies inside `outer`'s child area.
bool insideComposite(const Board &board, const Shape &inner)
{
    const Shape *parent = board.shapeByReference(inner.parentBoundary);
    if (!parent)
        return false;
    const QRectF area = parent->rect();
    return area.contains(inner.rect());
}

qreal overlapArea(const QRectF &a, const QRectF &b)
{
    const QRectF i = a.intersected(b);
    return i.isValid() ? i.width() * i.height() : 0;
}

// Laid out: no two shapes overlap, except a composite and what it holds;
// children are inside.
void verifyLaidOut(const Board &board, const QString &name)
{
    for (const auto &s : board.shapes()) {
        if (!s->parentBoundary.isEmpty())
            QVERIFY2(insideComposite(board, *s), qPrintable(name + ": " + s->key));
        for (const auto &t : board.shapes()) {
            const bool isNested =
                board.descendantShapes(s.get()).contains(t.get()) || board.descendantShapes(t.get()).contains(s.get());
            if (s == t || isNested)
                continue;
            QVERIFY2(!s->rect().intersects(t->rect()), qPrintable(name + ": " + s->key + " / " + t->key));
        }
    }
}

// Every shape of the board is in the copy, with the same text, nesting and
// geometry.
void verifySameShapes(const Board &board, const Board &again)
{
    for (const auto &s : board.shapes()) {
        const Shape *copy = byKey(again, s->key);
        QVERIFY2(copy, qPrintable(s->key));
        QCOMPARE(copy->type, s->type);
        QCOMPARE(copy->displayText(), s->displayText());
        QCOMPARE(copy->parentBoundary, s->parentBoundary);
        QVERIFY(std::abs(copy->x - s->x) < 0.1 && std::abs(copy->y - s->y) < 0.1);
        QVERIFY2(
            std::abs(copy->w - s->w) < 0.6 && std::abs(copy->h - s->h) < 0.6,
            qPrintable(QStringLiteral("%1 %2x%3 %4x%5").arg(s->key).arg(s->w).arg(s->h).arg(copy->w).arg(copy->h)));
    }
}

QStringList sortedLinkLabels(const Board &board)
{
    QStringList labels;
    for (const auto &l : board.links())
        labels.append(board.shape(l->from)->key + ">" + board.shape(l->to)->key + ":" + l->label);
    labels.sort();
    return labels;
}

} // namespace

class TestDiagramStates : public QObject {
    Q_OBJECT

private slots:

    void exposesUmlStatesAndPseudoStates()
    {
        const Family &family = families::states();
        QCOMPARE(family.id, QStringLiteral("states"));
        QCOMPARE(family.title, QStringLiteral("State diagram"));
        QVERIFY(family.graphvizClusters);
        QVERIFY(family.nestedCreation);
        QVERIFY(family.resolveLabels);
        QStringList types;
        for (const ElementPreset &preset : family.elements)
            types.append(preset.shapeType);
        QCOMPARE(types, (QStringList{"state", "stateContainer", "initial", "final", "choice", "fork", "join"}));
        for (const ElementPreset &preset : family.elements) {
            if (preset.shapeType == QLatin1String("choice"))
                QCOMPARE(preset.props.value("text").toString(), QStringLiteral("Condition?"));
        }
        QCOMPARE(family.links.size(), 1);
        QVERIFY(family.links[0].allowSelf);
        QVERIFY(family.links[0].allowMulti);
        QCOMPARE(mermaid().id(), QStringLiteral("mermaid-state"));
        QCOMPARE(plantuml().id(), QStringLiteral("plantuml-state"));
        QVERIFY(mermaid().matches(QStringLiteral("stateDiagram\n  A --> B")));
        QVERIFY(mermaid().matches(QStringLiteral("%% title\nstateDiagram-v2\n  A --> B")));
        QVERIFY(!mermaid().matches(QStringLiteral("flowchart TB\n  A --> B")));
        QVERIFY(plantuml().matches(QStringLiteral("@startuml\n[*] --> A\n@enduml")));
        QCOMPARE(codecFor(QStringLiteral("mermaid"), QStringLiteral("stateDiagram-v2\n  [*] --> A")), &mermaid());
        QCOMPARE(codecFor(QStringLiteral("plantuml"), QString::fromUtf8(kNestedPlantuml)), &plantuml());
    }

    // Every element of the palette survives both notations.
    void paletteElementsSurviveBothNotations()
    {
        const QStringList keys{"active", "composite", "start", "end", "decision", "parallel", "sync"};
        Board board;
        board.setStyle(Style::printed());
        board.setFamily(&families::states());
        QStringList ids;
        for (int i = 0; i < families::states().elements.size(); ++i) {
            const ElementPreset &preset = families::states().elements[i];
            QJsonObject props = preset.props;
            props.insert("key", keys[i]);
            const Shape *s = board.addShape(preset.shapeType, props);
            QVERIFY(s);
            ids.append(s->id);
        }
        for (const auto &[a, b] : std::vector<std::pair<int, int>>{{2, 0}, {0, 4}, {4, 5}, {5, 6}, {6, 3}})
            QVERIFY(board.addLink({}, ids[a], ids[b]));

        for (const Codec *codec : families::stateCodecs()) {
            const QString source = codec->serialize(board, QJsonObject{{"rankdir", "TB"}});
            QVERIFY2(source.contains("state \"Condition?\" as decision"), qPrintable(source));
            QVERIFY(source.contains("state decision <<choice>>"));
            QVERIFY(source.contains("state parallel <<fork>>"));
            QVERIFY(source.contains("state sync <<join>>"));
            QVERIFY(QRegularExpression("state composite \\{\\s*\\n\\s*\\}").match(source).hasMatch());
            const Model model = parsed(*codec, source);
            QSet<QString> types;
            for (const Model::Node &node : model.nodes)
                types.insert(node.type);
            QCOMPARE(types, (QSet<QString>{"state", "stateContainer", "initial", "final", "choice", "fork", "join"}));
            QCOMPARE(nodeBy(model, "sourceKey", "active")->props.value("text").toString(), QStringLiteral("New state"));
            QCOMPARE(nodeBy(model, "sourceKey", "decision")->props.value("text").toString(),
                     QStringLiteral("Condition?"));
            QCOMPARE(model.links.size(), 5);
            QVERIFY(model.warnings.isEmpty());
        }
    }

    void nestedCompositesKeepTheirChildrenAndPseudoStates()
    {
        for (const auto &[codec, body] :
             {std::pair{&mermaid(), kNestedMermaid}, std::pair{&plantuml(), kNestedPlantuml}}) {
            const Model model = parsed(*codec, QString::fromUtf8(body));
            const Model::Node *active = nodeBy(model, "sourceKey", "Active");
            const Model::Node *nested = nodeBy(model, "sourceKey", "Nested");
            const Model::Node *idle = nodeBy(model, "sourceKey", "Idle");
            const Model::Node *ready = nodeBy(model, "sourceKey", "Ready");
            QVERIFY(active && nested && idle && ready);
            QCOMPARE(active->type, QStringLiteral("stateContainer"));
            QCOMPARE(active->props.value("text").toString(), QStringLiteral("Operational"));
            QCOMPARE(nested->type, QStringLiteral("stateContainer"));
            QCOMPARE(nested->props.value("parentBoundary").toString(), active->key);
            QCOMPARE(idle->props.value("parentBoundary").toString(), active->key);
            QCOMPARE(ready->props.value("parentBoundary").toString(), nested->key);
            QCOMPARE(ready->key, QStringLiteral("Active.Nested.Ready"));
            QCOMPARE(countType(model, "initial"), 3);
            QCOMPARE(countType(model, "final"), 2);
            QVERIFY(model.warnings.isEmpty());

            Board board;
            build(board, *codec, QString::fromUtf8(body));
            const QString serialized = codec->serialize(board, QJsonObject{{"rankdir", "TB"}});
            QVERIFY2(QRegularExpression("state Active \\{[\\s\\S]*state Nested \\{").match(serialized).hasMatch(),
                     qPrintable(serialized));
            const Model reparsed = parsed(*codec, serialized);
            QCOMPARE(countType(reparsed, "stateContainer"), 2);
            QCOMPARE(nodeBy(reparsed, "sourceKey", "Ready")->props.value("parentBoundary").toString(),
                     nodeBy(reparsed, "sourceKey", "Nested")->key);
            // The same keys: the layout kept by key still applies.
            QStringList before;
            QStringList after;
            for (const Model::Node &node : model.nodes)
                before.append(node.key);
            for (const Model::Node &node : reparsed.nodes)
                after.append(node.key);
            before.sort();
            after.sort();
            QCOMPARE(after, before);
            QCOMPARE(reparsed.links.size(), model.links.size());
        }
    }

    void readsTheNotationsVariants()
    {
        // Self-transitions, reversed and styled PlantUML arrows, descriptions.
        const Model p = parsed(plantuml(), QStringLiteral("@startuml\n"
                                                          "state A : first line\n"
                                                          "A : second line\n"
                                                          "B <-- A : back\n"
                                                          "A -up-> C\n"
                                                          "A -[#red]-> D\n"
                                                          "A -> A : retry\n"
                                                          "state X as \"Long\\nname\"\n"
                                                          "note left of A : a note\n"
                                                          "note right of B\n  several\n  lines\nend note\n"
                                                          "A ==> ???\n"
                                                          "@enduml"));
        const Model::Node *a = nodeBy(p, "sourceKey", "A");
        QVERIFY(a);
        QCOMPARE(a->props.value("desc").toArray().size(), 2);
        QCOMPARE(nodeBy(p, "sourceKey", "X")->props.value("text").toString(), QStringLiteral("Long\nname"));
        QCOMPARE(p.links.size(), 4);
        QCOMPARE(p.links[0].from, QStringLiteral("A"));
        QCOMPARE(p.links[0].to, QStringLiteral("B"));
        QCOMPARE(p.links[0].props.value("label").toString(), QStringLiteral("back"));
        QCOMPARE(p.links[3].from, p.links[3].to);
        QCOMPARE(p.warnings, QStringList{"Not understood: A ==> ???"});

        const Model m = parsed(mermaid(), QStringLiteral("stateDiagram-v2\n"
                                                         "  direction LR\n"
                                                         "  classDef hot fill:#f00\n"
                                                         "  A:::hot --> B : go<br/>now\n"
                                                         "  note right of A\n    text\n  end note\n"
                                                         "  B --> B\n"
                                                         "  --\n"
                                                         "  what is this\n"));
        QCOMPARE(m.meta.value("rankdir").toString(), QStringLiteral("LR"));
        QCOMPARE(m.links.size(), 2);
        QCOMPARE(m.links[0].props.value("label").toString(), QStringLiteral("go\nnow"));
        QCOMPARE(m.warnings, QStringList{"Not understood: what is this"});
    }

    void serializesDirectionAndLonelyStates()
    {
        Board board;
        build(board, mermaid(), QStringLiteral("stateDiagram-v2\n  direction LR\n  state Alone\n  A --> B : x\\ny"));
        const QString source = mermaid().serialize(board, board.meta);
        QVERIFY2(source.contains("  direction LR"), qPrintable(source));
        QVERIFY(source.contains("state Alone"));
        QVERIFY(source.contains("A --> B : x\\ny"));
        const Model again = parsed(mermaid(), source);
        QCOMPARE(again.nodes.size(), qsizetype(3));
        QCOMPARE(again.links[0].props.value("label").toString(), QStringLiteral("x\ny"));
        QVERIFY(plantuml().serialize(board, board.meta).startsWith("@startuml\n"));
        QVERIFY(plantuml().serialize(board, board.meta).endsWith("\n@enduml"));
    }

    void compositeLeavesItsInteriorFreeForChildren()
    {
        StateContainerShape container;
        container.load(QJsonObject{{"x", 20}, {"y", 30}, {"w", 340}, {"h", 230}});
        QVERIFY(container.drawBehindLinks());
        QVERIFY(container.isCreationContainer());
        QVERIFY(container.isGraphvizCluster());
        QVERIFY(container.movesChildren());
        QVERIFY(container.hitTest(QPointF(25, 120)));   // the border selects it
        QVERIFY(container.hitTest(QPointF(190, 49)));   // so does the title
        QVERIFY(!container.hitTest(QPointF(190, 150))); // the middle is free to add a child
        QCOMPARE(container.text, QStringLiteral("Composite state"));
    }

    void childrenStayInsideAndMoveWithTheirComposite()
    {
        Board board;
        build(board, mermaid(), QString::fromUtf8(kNestedMermaid));
        QVERIFY(graphviz::autoArrange(board, familyOptions(board)));
        Shape *active = byKey(board, "Active");
        Shape *idle = byKey(board, "Active.Idle");
        Shape *ready = byKey(board, "Active.Nested.Ready");
        QVERIFY(active && idle && ready);
        // The layout places every child inside its composite.
        for (const auto &s : board.shapes()) {
            if (!s->parentBoundary.isEmpty())
                QVERIFY2(insideComposite(board, *s), qPrintable(s->key));
        }
        // Dragged out, a child is brought back inside.
        idle->x = active->x + active->w + 200;
        board.containShape(*idle);
        QVERIFY(active->childBounds(board)->contains(idle->rect()));
        // Moving a composite carries its descendants.
        const QList<Shape *> carried = board.descendantShapes(active);
        QVERIFY(carried.contains(ready));
        QVERIFY(carried.contains(byKey(board, "Active.Nested")));
        // A shape created in a composite joins it and stays in it.
        const Shape *created =
            board.createShapeAt(QPointF(active->cx(), active->y + active->h - 4), "state",
                                QJsonObject{{"text", "New state"}, {"parentBoundary", active->key}}, false);
        QVERIFY(created);
        QVERIFY(active->rect().contains(created->rect()));
    }

    void choiceShowsAndEditsItsConditionInADiamond()
    {
        Board board;
        board.setStyle(Style::printed());
        board.setFamily(&families::states());
        auto *choice = static_cast<ChoiceShape *>(
            board.addShape("choice", QJsonObject{{"text", "A detailed business condition to check?"}}));
        QVERIFY(choice->w > 96);
        QVERIFY(choice->h >= 72);
        QVERIFY(!choice->lines().isEmpty());
        QCOMPARE(choice->graphvizNodeAttributes(board, "TB", choice->w, choice->h),
                 (QStringList{"shape=diamond", "fixedsize=true", "margin=0", "label=\"\"",
                              "width=" + QString::number(choice->w / 72, 'f', 3),
                              "height=" + QString::number(choice->h / 72, 'f', 3)}));
        // Border points lie on the diamond.
        const QPointF right = choice->borderPoint(QPointF(choice->cx() + 500, choice->cy()));
        QCOMPARE(right, QPointF(choice->x + choice->w, choice->cy()));

        TextEditSpec spec;
        Shape *target = nullptr;
        board.setTextEditor([&](Shape *s, Link *, const TextEditSpec &edit) {
            target = s;
            spec = edit;
        });
        choice->onDoubleClick(board);
        QCOMPARE(target, choice);
        QCOMPARE(spec.get(), QStringLiteral("A detailed business condition to check?"));
        spec.set("  New condition?  ");
        QCOMPARE(choice->text, QStringLiteral("New condition?"));
        QCOMPARE(spec.align, Align::Center);

        // An empty choice is the bare UML diamond.
        const Shape *bare = board.addShape("choice", {});
        QCOMPARE(bare->w, 36.0);
        QCOMPARE(bare->h, 36.0);
    }

    void stateGrowsWithItsContent()
    {
        Board board;
        board.setStyle(Style::printed());
        board.setFamily(&families::states());
        auto *s = static_cast<StateShape *>(board.addShape("state", QJsonObject{{"text", "Ready"}}));
        const QSizeF compact(s->w, s->h);
        QCOMPARE(compact.width(), 150.0);
        s->text = "Detailed business validation";
        s->desc = {"This description is long enough that it has to stay entirely visible"};
        board.measureShape(*s);
        QVERIFY(s->w > compact.width());
        QVERIFY(s->h > compact.height());
        QVERIFY(s->w <= 360);

        // Editing: the first line names it, the next ones describe it.
        TextEditSpec spec;
        board.setTextEditor([&](Shape *, Link *, const TextEditSpec &edit) { spec = edit; });
        s->onDoubleClick(board);
        QVERIFY(!spec.enterCommits);
        spec.set(" Paid \n  card \n\n cash ");
        QCOMPARE(s->text, QStringLiteral("Paid"));
        QCOMPARE(s->desc, (QStringList{"card", "cash"}));
        QCOMPARE(spec.get(), QStringLiteral("Paid\ncard\ncash"));
    }

    void parallelTransitionsAndLoopsSeparateTheirLabels()
    {
        Board board;
        board.setStyle(Style::printed());
        board.setFamily(&families::states());
        const Shape *a = board.addShape("state", QJsonObject{{"x", 0}, {"y", 100}, {"text", "Active"}});
        const Shape *b = board.addShape("state", QJsonObject{{"x", 330}, {"y", 100}, {"text", "Done"}});
        Link *ab1 = board.addLink({}, a->id, b->id, QJsonObject{{"label", "Validate"}});
        Link *ab2 = board.addLink({}, a->id, b->id, QJsonObject{{"label", "Cancel"}});
        Link *loop1 = board.addLink({}, a->id, a->id, QJsonObject{{"label", "Retry"}});
        Link *loop2 = board.addLink({}, a->id, a->id, QJsonObject{{"label", "Wait\nthen retry"}});
        QVERIFY(ab1 && ab2 && loop1 && loop2 && ab1 != ab2 && loop1 != loop2);
        const auto box = [&](Link *l) {
            const QPointF p = l->labelPos(*l->route(board), board);
            return std::pair<qreal, qreal>{p.y(), l->labelBox(board).height};
        };
        const auto overlapsVertically = [](std::pair<qreal, qreal> left, std::pair<qreal, qreal> right) {
            return std::abs(left.first - right.first) < (left.second + right.second) / 2;
        };
        QVERIFY(!overlapsVertically(box(ab1), box(ab2)));
        QVERIFY(!overlapsVertically(box(loop1), box(loop2)));
        // The second loop is drawn outside the first.
        const auto *t1 = static_cast<TransitionLink *>(loop1);
        const auto *t2 = static_cast<TransitionLink *>(loop2);
        QVERIFY(t2->selfOffset(board) > t1->selfOffset(board));
        const auto top = [&](Link *l) {
            qreal y = 1e9;
            const Route route = *l->route(board);
            for (const QPointF &p : route.points)
                y = std::min(y, p.y());
            return y;
        };
        QVERIFY(top(loop2) < top(loop1));
        // A loop is hit along its arc, not only on its chord.
        const Route r = *loop1->route(board);
        QVERIFY(loop1->hitTest(board, r.points[8], 2));

        QImage image(800, 500, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        Canvas::paintBoard(painter, board);
    }

    // After a manual move, labels are spread along their own transitions.
    void resolvedLabelsStayOnTheirTransitions()
    {
        Board board;
        board.setStyle(Style::printed());
        board.setFamily(&families::states());
        Shape *a = board.addShape("state", QJsonObject{{"x", 0}, {"y", 0}, {"w", 150}, {"h", 60}, {"text", "A"}});
        Shape *b = board.addShape("state", QJsonObject{{"x", 360}, {"y", 0}, {"w", 150}, {"h", 60}, {"text", "B"}});
        Shape *c = board.addShape("state", QJsonObject{{"x", 180}, {"y", 240}, {"w", 150}, {"h", 60}, {"text", "C"}});
        Link *ac = board.addLink({}, a->id, c->id, QJsonObject{{"label", "Ambiguous request"}});
        Link *bc = board.addLink({}, b->id, c->id, QJsonObject{{"label", "No service identified"}});
        for (Link *l : {ac, bc}) {
            Spline spline;
            spline.points = {{75, 60}, {100, 120}, {180, 150}, {255, 240}};
            spline.end = QPointF(255, 240);
            spline.fromAnchor = QPointF(board.shape(l->from)->x, board.shape(l->from)->y);
            spline.toAnchor = QPointF(c->x, c->y);
            l->spline = spline;
        }
        board.resolveLabels();
        QList<QRectF> boxes;
        for (const Link *l : {ac, bc}) {
            const Route r = *l->route(board);
            const QPointF p = l->labelPos(r, board);
            QVERIFY2(l->distanceToRoute(r, p) < 1e-6, "a label's center stays exactly on its curve");
            const Link::LabelBox lb = l->labelBox(board);
            boxes.append(QRectF(p.x() - lb.width / 2, p.y() - lb.height / 2, lb.width, lb.height));
        }
        QCOMPARE(overlapArea(boxes[0], boxes[1]), 0.0);
        for (const QRectF &labelBox : boxes) {
            for (const Shape *s : {a, b, c})
                QCOMPARE(overlapArea(labelBox, s->rect()), 0.0);
        }
        // Moving an end drops the placement with the curve.
        a->x += 40;
        const Route moved = *ac->route(board);
        QVERIFY(ac->spline == std::nullopt);
        QCOMPARE(ac->labelPos(moved, board), (moved.a + moved.b) / 2);
    }

    void layoutKeepsCompositesAsClusters()
    {
        Board board;
        board.setStyle(Style::printed());
        board.setFamily(&families::states());
        const Shape *container =
            board.addShape("stateContainer", QJsonObject{{"key", "Active"}, {"text", "Active"}, {"x", 0}, {"y", 0}});
        const Shape *child = board.addShape(
            "state",
            QJsonObject{{"key", "Active.Idle"}, {"sourceKey", "Idle"}, {"text", "Idle"}, {"parentBoundary", "Active"}});
        const Shape *outside = board.addShape("state", QJsonObject{{"key", "Done"}, {"text", "Done"}});
        const Link *exit = board.addLink({}, container->id, outside->id);
        QVERIFY(graphviz::autoArrange(board, {QStringLiteral("LR"), {}, {}, true}));
        QVERIFY(container->rect().contains(child->rect()));
        QVERIFY(!container->rect().intersects(outside->rect()));
        // Holding a child, it is sized on it, not on an empty composite's floor.
        QVERIFY(container->w < 300);
        QVERIFY(container->w >= text::width(Font(14, 600), container->displayText()) + 4 * 14);
        // The transition leaves from the composite's real border.
        const Route r = *exit->route(board);
        QVERIFY(std::abs(r.a.x() - (container->x + container->w)) < 2 ||
                container->rect().adjusted(-2, -2, 2, 2).contains(r.a));
    }

    void documentsRoundTripWithTheirLayout()
    {
        const QList<std::pair<QString, QByteArray>> files{
            {"blank.mmd", document::blank("states")},
            {"rich.mmd", QByteArray(kRichMermaid)},
            {"nested.puml", QByteArray(kNestedPlantuml)},
        };
        QVERIFY(document::blank("states").startsWith("stateDiagram-v2"));
        QCOMPARE(document::extension("states"), QStringLiteral("mmd"));
        for (const auto &[name, content] : files) {
            Board board;
            document::Loaded loaded;
            QVERIFY2(load(board, name, content, &loaded), qPrintable(name));
            QVERIFY(loaded.needsArrange);
            QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(", ")));
            QVERIFY(graphviz::autoArrange(board, familyOptions(board)));

            verifyLaidOut(board, name);
            if (QTest::currentTestFailed())
                return;
            QImage image(1200, 900, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            {
                QPainter painter(&image);
                Canvas::paintBoard(painter, board);
            }

            const Codec *codec = loaded.codec;
            const QByteArray saved = document::save(board, *codec);
            Board again;
            document::Loaded reloaded;
            QVERIFY(load(again, name, saved, &reloaded));
            QVERIFY2(!reloaded.needsArrange, qPrintable(name));
            QVERIFY(reloaded.unplaced.isEmpty());
            QCOMPARE(again.shapes().size(), board.shapes().size());
            QCOMPARE(again.links().size(), board.links().size());
            verifySameShapes(board, again);
            if (QTest::currentTestFailed())
                return;
            // Written scope by scope: the order may change, not the relations.
            QCOMPARE(sortedLinkLabels(again), sortedLinkLabels(board));
            QCOMPARE(document::source(again, *codec), document::source(board, *codec));
        }
    }

    void undoRebuildsStateElements()
    {
        Board board;
        build(board, mermaid(), QString::fromUtf8(kRichMermaid));
        QVERIFY(graphviz::autoArrange(board, familyOptions(board)));
        const QByteArray before = board.snapshot();
        board.edit([&] {
            auto *waiting = static_cast<StateShape *>(byKey(board, "Waiting"));
            waiting->desc.append("extra");
        });
        board.undo();
        QCOMPARE(board.snapshot(), before);
        const auto *waiting = static_cast<StateShape *>(byKey(board, "Waiting"));
        QCOMPARE(waiting->desc, (QStringList{"order placed", "reminder after 24 h"}));
        QCOMPARE(waiting->sourceKey, QStringLiteral("Waiting"));
        const Shape *packed = byKey(board, "Shipping.Packed");
        QVERIFY(packed);
        QCOMPARE(packed->parentBoundary, QStringLiteral("Shipping"));
    }

    // A copy has a key of its own: it is written under that key, so that it
    // keeps its place when the file is read again.
    void copiesKeepTheirOwnKeys()
    {
        Board board;
        build(board, mermaid(), QString::fromUtf8(kNestedMermaid));
        board.selectShape(byKey(board, "Active.Idle")->id);
        board.duplicateSelection();
        const Shape *copy = board.selectedShape();
        QVERIFY(copy && copy->key != QStringLiteral("Active.Idle"));
        const QString source = mermaid().serialize(board, board.meta);
        const Model model = parsed(mermaid(), source);
        bool found = false;
        for (const Model::Node &node : model.nodes)
            found = found || node.key == copy->key;
        QVERIFY2(found, qPrintable(copy->key + "\n" + source));
    }
};

QTEST_MAIN(TestDiagramStates)
#include "tst_diagram_states.moc"
