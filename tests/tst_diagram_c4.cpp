// The C4 architecture family (sidecar's c4-diagram.js and fence/mermaid-c4.js):
// its presets, measures, Mermaid codec, layout and drawing.

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Canvas.h"
#include "viewers/diagram/Document.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/Graphviz.h"
#include "viewers/diagram/families/Families.h"
#include "viewers/diagram/families/c4/C4.h"

#include <QImage>
#include <QPainter>
#include <QtTest>

using namespace diagram;
using namespace diagram::c4;

namespace {

// diagram-families.test.js's fixture.
const char *const kContainer = R"(C4Container
  title Test architecture
  Person(user, "User", "Manages their documents")
  Container_Boundary(platform, "Platform") {
    Container(web, "Application", "JavaScript", "Web interface")
    ContainerDb(data, "Data", "PostgreSQL", "Main storage")
  }
  SystemQueue_Ext(api, "External bus", "Third-party service")
  Rel(user, web, "Uses")
  Rel(web, api, "Publishes", "JSON")
)";

const char *const kRich = R"(C4Container
  title Online shop
  Person(customer, "Customer", "Buys products")
  Person_Ext(support, "Support agent", "Helps customers")
  Enterprise_Boundary(shop, "Shop") {
    System_Boundary(store, "Store") {
      Container(spa, "Single-page app", "TypeScript, React", "Lets customers browse and order")
      Container(api, "API", "Kotlin", "Serves the catalog and the orders")
      ContainerDb(db, "Database", "PostgreSQL", "Products, orders and customers")
      ContainerQueue(events, "Events", "Kafka", "Domain events")
    }
  }
  System_Ext(payments, "Payment provider", "Takes the payments")
  Rel(customer, spa, "Uses", "HTTPS")
  Rel(spa, api, "Calls", "REST/JSON")
  Rel(api, db, "Reads and writes", "SQL")
  Rel(api, events, "Publishes", "AMQP", "Order placed and shipped")
  BiRel(api, payments, "Exchanges with")
  Rel(support, api, "Uses")
)";

const Codec &codec()
{
    const Codec *c = findCodec(QStringLiteral("mermaid-c4"));
    Q_ASSERT(c);
    return *c;
}

Shape *byKey(const Board &board, const QString &key)
{
    for (const auto &s : board.shapes()) {
        if (s->key == key)
            return s.get();
    }
    return nullptr;
}

bool load(Board &board, const QByteArray &content, document::Loaded *loaded = nullptr)
{
    document::Loaded result;
    QString error;
    const bool ok = document::load(board, QStringLiteral("c4.mmd"), content, result, &error);
    if (!ok)
        qWarning() << error;
    if (loaded)
        *loaded = result;
    return ok;
}

graphviz::ArrangeOptions familyOptions(const Board &board)
{
    const Family &f = families::c4();
    graphviz::ArrangeOptions options;
    options.rankdir = board.meta.value(QStringLiteral("rankdir")).toString(QStringLiteral("LR"));
    options.clusters = f.graphvizClusters;
    options.inferClusterMembership = f.inferClusterMembership;
    options.stackDisconnected = f.graphvizStackDisconnected;
    options.adaptiveDirection = f.graphvizAdaptiveDirection;
    options.splines = f.graphvizSplines;
    options.viewSize = QSizeF(1200, 800);
    return options;
}

bool contains(const QRectF &outer, const QRectF &inner)
{
    return inner.left() >= outer.left() - 0.5 && inner.top() >= outer.top() - 0.5
           && inner.right() <= outer.right() + 0.5 && inner.bottom() <= outer.bottom() + 0.5;
}

} // namespace

class TestDiagramC4 : public QObject {
    Q_OBJECT

private slots:
    void exposesEveryElementAndRelation()
    {
        const Family &f = families::c4();
        QCOMPARE(f.id, QStringLiteral("c4"));
        QCOMPARE(f.title, QStringLiteral("C4 architecture"));
        QVERIFY(f.graphvizClusters);
        QVERIFY(f.graphvizAdaptiveDirection);
        QCOMPARE(f.graphvizSplines, QStringLiteral("spline"));
        QVERIFY(f.nestedCreation);
        QSet<QString> ids;
        for (const ElementPreset &preset : f.elements)
            ids.insert(preset.id);
        for (const char *id : {"person", "person-external", "system", "system-database", "system-queue", "container",
                               "container-database", "container-queue", "component", "component-database",
                               "component-queue", "component-queue-external", "enterprise-boundary",
                               "system-boundary", "container-boundary", "deployment-boundary"})
            QVERIFY2(ids.contains(QString::fromLatin1(id)), id);
        QCOMPARE(f.elements.size(), 24);
        QCOMPARE(ids.size(), 24);
        QVERIFY(f.technologies.contains(QStringLiteral("REST/JSON")));
        QVERIFY(f.technologies.contains(QStringLiteral("WebSocket")));
        QCOMPARE(f.relations.size(), 10);
        bool both = false;
        for (const RelationPreset &preset : f.relations) {
            QVERIFY(!preset.props.value(QStringLiteral("label")).toString().isEmpty());
            both = both || preset.props.value(QStringLiteral("bidirectional")).toBool();
        }
        QVERIFY(both);
        // Every preset builds.
        Board board;
        board.setFamily(&f);
        for (const ElementPreset &preset : f.elements)
            QVERIFY(board.addShape(preset.shapeType, preset.props));

        // A relation always says what it does.
        Shape *a = board.addShape({}, {});
        Shape *b = board.addShape({}, {});
        Link *l = board.addLink(QStringLiteral("c4relation"), a->id, b->id, {});
        QVERIFY(l);
        QCOMPARE(l->label, QStringLiteral("Uses"));
    }

    void readsTheMermaidC4Syntax()
    {
        const Codec *c = codecFor(QStringLiteral("mermaid"), QString::fromUtf8(kContainer));
        QVERIFY(c);
        QCOMPARE(c->id(), QStringLiteral("mermaid-c4"));
        QVERIFY(c->matches(QStringLiteral("%% a comment\nC4Context\n  Person(a, \"A\")")));
        QVERIFY(!c->matches(QStringLiteral("flowchart TB\n  A --> B")));
        Model model;
        QVERIFY(c->parse(QString::fromUtf8(kContainer), model, nullptr));
        QCOMPARE(model.nodes.size(), 5);
        QCOMPARE(model.links.size(), 2);
        QVERIFY(model.warnings.isEmpty());
        QCOMPARE(model.meta.value(QStringLiteral("title")).toString(), QStringLiteral("Test architecture"));
        QCOMPARE(model.meta.value(QStringLiteral("family")).toString(), QStringLiteral("C4Container"));
        QCOMPARE(model.meta.value(QStringLiteral("rankdir")).toString(), QStringLiteral("LR"));
        QCOMPARE(model.links[1].props.value(QStringLiteral("technology")).toString(), QStringLiteral("JSON"));
        bool boundary = false, database = false, externalQueue = false;
        for (const Model::Node &node : model.nodes) {
            boundary = boundary || node.type == QLatin1String("c4boundary");
            database = database || node.props.value(QStringLiteral("variant")).toString() == QLatin1String("database");
            externalQueue = externalQueue
                            || (node.props.value(QStringLiteral("variant")).toString() == QLatin1String("queue")
                                && node.props.value(QStringLiteral("external")).toBool());
            if (node.key == QLatin1String("web")) {
                QCOMPARE(node.props.value(QStringLiteral("parentBoundary")).toString(), QStringLiteral("platform"));
                QCOMPARE(node.props.value(QStringLiteral("technology")).toString(), QStringLiteral("JavaScript"));
                QCOMPARE(node.props.value(QStringLiteral("description")).toString(), QStringLiteral("Web interface"));
            }
            if (node.key == QLatin1String("api")) {
                QVERIFY(!node.props.contains(QStringLiteral("parentBoundary")));
                QCOMPARE(node.props.value(QStringLiteral("description")).toString(),
                         QStringLiteral("Third-party service"));
            }
        }
        QVERIFY(boundary);
        QVERIFY(database);
        QVERIFY(externalQueue);
    }

    void readsIndexedRelationsNamedArgumentsAndNodes()
    {
        Model model;
        QVERIFY(codec().parse(QStringLiteral("C4Dynamic\n"
                                             "  Node(cloud, \"Cloud\") {\n"
                                             "    Container(a, \"A\", $techn=\"Go\")\n"
                                             "  }\n"
                                             "  Component(b, \"B, the second\")\n"
                                             "  RelIndex(1, a, b, \"Calls\", \"gRPC\")\n"
                                             "  UpdateRelStyle(a, b, $textColor=\"red\")\n"
                                             "  Frobnicate a b\n"),
                              model, nullptr));
        QCOMPARE(model.nodes.size(), 3);
        QCOMPARE(model.nodes[0].props.value(QStringLiteral("boundaryKind")).toString(),
                 QStringLiteral("Deployment_Node"));
        QCOMPARE(model.nodes[1].props.value(QStringLiteral("technology")).toString(), QStringLiteral("Go"));
        QCOMPARE(model.nodes[2].props.value(QStringLiteral("name")).toString(), QStringLiteral("B, the second"));
        QCOMPARE(model.links.size(), 1);
        QCOMPARE(model.links[0].from, QStringLiteral("a"));
        QCOMPARE(model.links[0].props.value(QStringLiteral("technology")).toString(), QStringLiteral("gRPC"));
        // The styling statement is known; the other one is reported.
        QCOMPARE(model.warnings, QStringList{QStringLiteral("Not understood: Frobnicate a b")});
    }

    void writesTheSourceBack()
    {
        Model model;
        QVERIFY(codec().parse(QString::fromUtf8(kRich), model, nullptr));
        Board board;
        const QJsonObject meta = buildBoard(board, codec(), model);
        const QString serialized = codec().serialize(board, meta);
        QVERIFY(serialized.startsWith(QStringLiteral("C4Container\n  title Online shop\n")));
        QVERIFY(serialized.contains(QStringLiteral("  Enterprise_Boundary(shop, \"Shop\") {\n"
                                                   "    System_Boundary(store, \"Store\") {\n"
                                                   "      Container(spa, \"Single-page app\", \"TypeScript, React\", "
                                                   "\"Lets customers browse and order\")\n")));
        QVERIFY(serialized.contains(QStringLiteral("  BiRel(api, payments, \"Exchanges with\")")));
        QVERIFY(serialized.contains(
            QStringLiteral("  Rel(api, events, \"Publishes\", \"AMQP\", \"Order placed and shipped\")")));
        QVERIFY(serialized.contains(QStringLiteral("  Person_Ext(support, \"Support agent\", \"Helps customers\")")));
        QVERIFY(serialized.contains(QStringLiteral("ContainerQueue(events, \"Events\", \"Kafka\", \"Domain events\")")));

        // Parsed again, the same model.
        Model again;
        QVERIFY(codec().parse(serialized, again, nullptr));
        QCOMPARE(again.nodes.size(), model.nodes.size());
        QCOMPARE(again.links.size(), model.links.size());
        for (const Model::Node &node : model.nodes) {
            const auto it = std::find_if(again.nodes.begin(), again.nodes.end(),
                                         [&](const Model::Node &n) { return n.key == node.key; });
            QVERIFY(it != again.nodes.end());
            QCOMPARE(it->type, node.type);
            QCOMPARE(it->props, node.props);
        }
        for (qsizetype i = 0; i < model.links.size(); ++i) {
            QCOMPARE(again.links[i].from, model.links[i].from);
            QCOMPARE(again.links[i].to, model.links[i].to);
            QCOMPARE(again.links[i].props, model.links[i].props);
        }

        // A description without a technology keeps its place.
        Board edited;
        edited.setFamily(&families::c4());
        Shape *a = edited.addShape({}, {{QStringLiteral("key"), QStringLiteral("a")}});
        Shape *b = edited.addShape({}, {{QStringLiteral("key"), QStringLiteral("b")}});
        edited.addLink(QStringLiteral("c4relation"), a->id, b->id,
                       {{QStringLiteral("label"), QStringLiteral("Calls")},
                        {QStringLiteral("description"), QStringLiteral("Every night")}});
        QVERIFY(codec().serialize(edited, {}).contains(QStringLiteral("Rel(a, b, \"Calls\", \"\", \"Every night\")")));
        // An element whose boundary is gone is still written.
        a->parentBoundary = QStringLiteral("nowhere");
        QVERIFY(codec().serialize(edited, {}).contains(QStringLiteral("  System(a, ")));
    }

    void measuresEveryLineBeforeDrawing()
    {
        Board board;
        board.setFamily(&families::c4());
        auto *shape = static_cast<C4ElementShape *>(board.addShape(
            QStringLiteral("c4"),
            {{QStringLiteral("name"), QStringLiteral("Extremely detailed synchronization service")},
             {QStringLiteral("kind"), QStringLiteral("container")},
             {QStringLiteral("technology"), QStringLiteral("JavaScript-PostgreSQL-WebSocket-HTTPS-and-more")},
             {QStringLiteral("description"),
              QStringLiteral("Synchronizes the documents with a deliberately long responsibility.")}}));
        QVERIFY(shape);
        const qreal inner = shape->w - 40;
        const auto fits = [&](const Font &font, const QStringList &lines) {
            for (const QString &line : lines)
                QVERIFY2(text::width(font, line) <= inner, qPrintable(line));
        };
        fits(Font(15, 600), shape->titleLines());
        fits(Font(11.5), shape->typeLines());
        fits(Font(11.5), shape->technologyLines());
        fits(Font(13), shape->descriptionLines());
        QVERIFY(shape->titleLines().size() > 1 || shape->descriptionLines().size() > 1);
        QVERIFY(shape->h > 104);
        QVERIFY(shape->w >= 210 && shape->w <= 300);
        QCOMPARE(shape->typeLines(), QStringList{QStringLiteral("[Container]")});

        // A saved, too small size is measured again without moving it.
        auto *saved = static_cast<C4ElementShape *>(board.addShape(
            QStringLiteral("c4"),
            {{QStringLiteral("x"), 240}, {QStringLiteral("y"), 90}, {QStringLiteral("w"), 80}, {QStringLiteral("h"), 40},
             {QStringLiteral("name"), shape->name}, {QStringLiteral("kind"), QStringLiteral("container")},
             {QStringLiteral("description"), shape->description}}));
        QCOMPARE(saved->x, 240.0);
        QCOMPARE(saved->y, 90.0);
        QVERIFY(saved->w > 80);
        QVERIFY(saved->h > 40);
        for (const QString &line : saved->descriptionLines())
            QVERIFY(text::width(Font(13), line) <= saved->w - 40);

        // Resized narrower by hand, the box keeps the height its text needs.
        const qreal before = saved->h;
        saved->resizeTo(60, 30, board);
        QVERIFY(saved->h >= before);
    }

    void editsTheDatabaseBodyBelowItsCap()
    {
        Board board;
        board.setFamily(&families::c4());
        Shape *shape = board.addShape(QStringLiteral("c4"), {{QStringLiteral("x"), 40},
                                                             {QStringLiteral("y"), 50},
                                                             {QStringLiteral("name"), QStringLiteral("Transactions")},
                                                             {QStringLiteral("kind"), QStringLiteral("container")},
                                                             {QStringLiteral("variant"), QStringLiteral("database")}});
        std::optional<TextEditSpec> editor;
        Shape *target = nullptr;
        board.setTextEditor([&](Shape *s, Link *, const TextEditSpec &spec) {
            target = s;
            editor = spec;
        });
        shape->onDoubleClick(board);
        QVERIFY(editor);
        QCOMPARE(target, shape);
        QVERIFY(editor->rect().top() >= shape->y + 32);
        QVERIFY(!editor->enterCommits);
        QCOMPARE(editor->get(), QStringLiteral("Transactions\n\n"));
        editor->set(QStringLiteral("  Ledger \n PostgreSQL\nAll the money\nmoves"));
        auto *element = static_cast<C4ElementShape *>(shape);
        QCOMPARE(element->name, QStringLiteral("Ledger"));
        QCOMPARE(element->technology, QStringLiteral("PostgreSQL"));
        QCOMPARE(element->description, QStringLiteral("All the money\nmoves"));
        editor->set(QString());
        QCOMPARE(element->name, QStringLiteral("Element"));
    }

    void keepsTheBoundaryInsideFreeForCreation()
    {
        Board board;
        board.setFamily(&families::c4());
        Shape *boundary = board.addShape(QStringLiteral("c4boundary"), {{QStringLiteral("x"), 20},
                                                                        {QStringLiteral("y"), 30},
                                                                        {QStringLiteral("w"), 420},
                                                                        {QStringLiteral("h"), 260}});
        QVERIFY(!boundary->hitTest(QPointF(220, 150)));
        QVERIFY(boundary->hitTest(QPointF(23, 150)));
        QVERIFY(boundary->hitTest(QPointF(220, 33)));
        QVERIFY(boundary->hitTest(QPointF(60, 22))); // its title, above the frame
        QVERIFY(boundary->drawBehindLinks());
        QVERIFY(boundary->isGraphvizCluster());
        QVERIFY(boundary->isCreationContainer());
        QCOMPARE(board.containerAt(QPointF(220, 150)), boundary);
        QCOMPARE(boundary->layoutLabelFontPx(), 12.0);
        QCOMPARE(boundary->layoutLabel(), QStringLiteral("Boundary · Software system"));
        QVERIFY(boundary->layoutMinimumSize()->width() >= 340);
        QCOMPARE(boundary->layoutMinimumSize()->height(), 220.0);
        // A fresh boundary has room for its title, never less than 340 × 220.
        Shape *small = board.addShape(QStringLiteral("c4boundary"),
                                      {{QStringLiteral("w"), 10}, {QStringLiteral("h"), 10}});
        QVERIFY(small->w >= 340 && small->h >= 220);
    }

    void reservesThePaintedRelationPanel()
    {
        Board board;
        board.setFamily(&families::c4());
        Shape *a = board.addShape({}, {});
        Shape *b = board.addShape({}, {});
        Link *l = board.addLink(
            QStringLiteral("c4relation"), a->id, b->id,
            {{QStringLiteral("label"), QStringLiteral("Publishes the domain events on the message bus for everyone")},
             {QStringLiteral("technology"), QStringLiteral("AMQP")}});
        auto *relation = static_cast<C4RelationLink *>(l);
        const QStringList painted = relation->displayLines(board);
        QVERIFY(painted.size() > 2);
        QCOMPARE(painted.last(), QStringLiteral("[AMQP]"));
        const QString panel = relation->graphvizHtmlLabel(board);
        QVERIFY(panel.startsWith(QStringLiteral("<TABLE BORDER=\"1\"")));
        QVERIFY(panel.contains(QStringLiteral("CELLPADDING=\"6\"")));
        QCOMPARE(panel.split(QStringLiteral("<BR/>")).size(), painted.size());
        for (const QString &line : painted)
            QVERIFY2(panel.contains(line), qPrintable(line));
        QCOMPARE(relation->labelBox(board).lines, painted);
        QCOMPARE(relation->layoutLabel(board), painted.join(QLatin1Char('\n')));
        QVERIFY(relation->graphvizAttributes(board).isEmpty());
        relation->bidirectional = true;
        QCOMPARE(relation->graphvizAttributes(board),
                 (QStringList{QStringLiteral("dir=both"), QStringLiteral("arrowtail=normal")}));
        // Shown in the inspector, beside the label.
        QCOMPARE(relation->properties().size(), 3);
        relation->setPropertyValue(QStringLiteral("technology"), QStringLiteral("gRPC"));
        QCOMPARE(relation->propertyValue(QStringLiteral("technology")).toString(), QStringLiteral("gRPC"));
        // Undo rebuilds it from its JSON.
        const QJsonObject json = relation->toJson();
        C4RelationLink copy;
        copy.load(json);
        QCOMPARE(copy.toJson(), json);
    }

    void loadsSavesAndLoadsAgain()
    {
        Board blank;
        document::Loaded loaded;
        QVERIFY(load(blank, document::blank(QStringLiteral("c4")), &loaded));
        QCOMPARE(loaded.codec->id(), QStringLiteral("mermaid-c4"));
        QCOMPARE(blank.shapes().size(), size_t(2));
        QCOMPARE(blank.links().size(), size_t(1));
        QVERIFY(loaded.warnings.isEmpty());
        QCOMPARE(document::extension(QStringLiteral("c4")), QStringLiteral("mmd"));

        Board board;
        QVERIFY(load(board, kRich, &loaded));
        QVERIFY(loaded.needsArrange);
        QVERIFY(graphviz::autoArrange(board, familyOptions(board)));
        // A shape is painted, and the element too.
        byKey(board, QStringLiteral("api"))->customFill = QStringLiteral("#ffccbc");
        const QByteArray saved = document::save(board, *loaded.codec);
        QVERIFY(saved.startsWith("C4Container\n"));
        QVERIFY(saved.contains("%% duet {"));

        Board again;
        document::Loaded reloaded;
        QVERIFY(load(again, saved, &reloaded));
        QVERIFY(!reloaded.needsArrange);
        QCOMPARE(again.shapes().size(), board.shapes().size());
        QCOMPARE(again.links().size(), board.links().size());
        for (const auto &s : board.shapes()) {
            const Shape *t = byKey(again, s->key);
            QVERIFY2(t, qPrintable(s->key));
            QCOMPARE(t->type, s->type);
            QCOMPARE(t->displayText(), s->displayText());
            QCOMPARE(t->parentBoundary, s->parentBoundary);
            QCOMPARE(t->customFill, s->customFill);
            QVERIFY2(std::abs(t->x - s->x) <= 0.06 && std::abs(t->y - s->y) <= 0.06 && std::abs(t->w - s->w) <= 0.06 && std::abs(t->h - s->h) <= 0.06,
                     qPrintable(QStringLiteral("%1 %2,%3 %4x%5 -> %6,%7 %8x%9").arg(s->key).arg(s->x).arg(s->y).arg(s->w).arg(s->h).arg(t->x).arg(t->y).arg(t->w).arg(t->h)));
            QCOMPARE(t->toJson().value(QStringLiteral("technology")), s->toJson().value(QStringLiteral("technology")));
            QCOMPARE(t->toJson().value(QStringLiteral("description")),
                     s->toJson().value(QStringLiteral("description")));
        }
        for (size_t i = 0; i < board.links().size(); ++i) {
            const auto *a = static_cast<const C4RelationLink *>(board.links()[i].get());
            const auto *b = static_cast<const C4RelationLink *>(again.links()[i].get());
            QCOMPARE(b->label, a->label);
            QCOMPARE(b->technology, a->technology);
            QCOMPARE(b->description, a->description);
            QCOMPARE(b->bidirectional, a->bidirectional);
            QCOMPARE(again.shape(b->from)->key, board.shape(a->from)->key);
            QCOMPARE(again.shape(b->to)->key, board.shape(a->to)->key);
        }
        QCOMPARE(again.meta.value(QStringLiteral("title")).toString(), QStringLiteral("Online shop"));
    }

    void laysBoundariesAroundTheirElements()
    {
        Board board;
        QVERIFY(load(board, kRich));
        QVERIFY(graphviz::autoArrange(board, familyOptions(board)));
        std::vector<const Shape *> elements;
        for (const auto &s : board.shapes()) {
            if (s->type == QLatin1String("c4"))
                elements.push_back(s.get());
            if (s->parentBoundary.isEmpty())
                continue;
            const Shape *parent = board.shapeByReference(s->parentBoundary);
            QVERIFY(parent);
            QVERIFY2(contains(parent->rect(), s->rect()), qPrintable(s->key));
        }
        for (size_t i = 0; i < elements.size(); ++i) {
            for (size_t j = i + 1; j < elements.size(); ++j)
                QVERIFY2(!elements[i]->rect().intersects(elements[j]->rect()),
                         qPrintable(elements[i]->key + QStringLiteral(" / ") + elements[j]->key));
        }
        // Outside elements stay out of the boundaries.
        const QRectF shop = byKey(board, QStringLiteral("shop"))->rect();
        QVERIFY(!shop.intersects(byKey(board, QStringLiteral("payments"))->rect()));
        QVERIFY(!shop.intersects(byKey(board, QStringLiteral("customer"))->rect()));
        for (const auto &l : board.links())
            QVERIFY(l->spline);
    }

    void drawsWithoutCrashing()
    {
        Board board;
        QVERIFY(load(board, kRich));
        QVERIFY(graphviz::autoArrange(board, familyOptions(board)));
        for (const auto &s : board.shapes()) {
            if (s->key == QLatin1String("spa"))
                s->customFill = QStringLiteral("#c8e6c9");
            if (s->key == QLatin1String("store"))
                s->customFill = QStringLiteral("#bbdefb");
        }
        const QRectF bounds = board.contentBounds();
        QVERIFY(bounds.width() > 0);
        QImage image(bounds.size().toSize() + QSize(40, 40), QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(-bounds.topLeft() + QPointF(20, 20));
        Canvas::paintBoard(painter, board);
        // Selected and edited states too.
        for (const auto &s : board.shapes()) {
            s->draw(painter, board, ShapeState{true, false, false});
            s->draw(painter, board, ShapeState{false, true, false});
        }
        painter.end();
        QVERIFY(!image.isNull());
    }
};

QTEST_MAIN(TestDiagramC4)
#include "tst_diagram_c4.moc"
