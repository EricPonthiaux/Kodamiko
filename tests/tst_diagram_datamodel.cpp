// SPDX-License-Identifier: GPL-3.0-only

// The data model family (sidecar's erd-diagram.js and fence/mermaid-er.js):
// the Mermaid erDiagram codec, the tables and their columns, the relations
// joining FK and PK columns, the layout and the drawing.

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Canvas.h"
#include "viewers/diagram/Document.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/Graphviz.h"
#include "viewers/diagram/families/Families.h"
#include "viewers/diagram/families/datamodel/DataModel.h"

#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QtTest>

#include <optional>

using namespace diagram;
using namespace diagram::datamodel;

namespace {

const char *const kFixture = R"(erDiagram
      CUSTOMER {
        uuid id PK
        string email UK "Adresse de connexion"
      }
      ORDER {
        uuid id PK
        uuid customer_id FK
      }
      CUSTOMER ||--o{ ORDER : places)";

const Codec &codec()
{
    const Codec *c = findCodec(QStringLiteral("mermaid-er"));
    Q_ASSERT(c);
    return *c;
}

Model parse(const QString &source)
{
    Model model;
    QString error;
    if (!codec().parse(source, model, &error))
        qFatal("cannot parse the test model: %s", qPrintable(error));
    return model;
}

Shape *byKey(const Board &board, const QString &key)
{
    for (const auto &s : board.shapes()) {
        if (s->key == key)
            return s.get();
    }
    return nullptr;
}

bool loadInto(Board &board, const QByteArray &content, document::Loaded *loaded = nullptr)
{
    QString error;
    std::optional<document::Loaded> result = document::load(board, QStringLiteral("model.mmd"), content, &error);
    if (!result) {
        qWarning() << error;
        return false;
    }
    if (loaded)
        *loaded = std::move(*result);
    return true;
}

// What a test table is made of.
struct Table {
    QString id;
    QPointF position;
    QString name;
    QStringList attributes;
};

EntityShape *addTable(Board &board, const Table &table)
{
    Shape *s = board.addShape(QStringLiteral("entity"),
                              QJsonObject{{QStringLiteral("id"), table.id},
                                          {QStringLiteral("x"), table.position.x()},
                                          {QStringLiteral("y"), table.position.y()},
                                          {QStringLiteral("name"), table.name},
                                          {QStringLiteral("attributes"), toJsonArray(table.attributes)}});
    return dynamic_cast<EntityShape *>(s);
}

// The board of sidecar's tests: CUSTOMER on the left, ORDER on the right,
// ORDER.customer_id referencing CUSTOMER.id.
struct CustomerOrder {
    Board board;
    EntityShape *customer = nullptr;
    EntityShape *order = nullptr;
    RelationshipLink *relation = nullptr;

    CustomerOrder()
    {
        board.setFamily(&families::dataModel());
        customer = addTable(board, {.id = QStringLiteral("customer"),
                                    .position = {10, 20},
                                    .name = QStringLiteral("CUSTOMER"),
                                    .attributes = {QStringLiteral("uuid id PK"), QStringLiteral("string email UK")}});
        order = addTable(board, {.id = QStringLiteral("order"),
                                 .position = {500, 70},
                                 .name = QStringLiteral("ORDER"),
                                 .attributes = {QStringLiteral("uuid id PK"), QStringLiteral("uuid customer_id FK"),
                                                QStringLiteral("decimal total")}});
        relation = dynamic_cast<RelationshipLink *>(
            board.addLink(QStringLiteral("relationship"), customer->id, order->id,
                          QJsonObject{{QStringLiteral("fromCard"), QStringLiteral("||")},
                                      {QStringLiteral("toCard"), QStringLiteral("o{")},
                                      {QStringLiteral("label"), QStringLiteral("passe")}}));
    }
};

bool overlaps(const Board &board)
{
    for (const auto &a : board.shapes()) {
        for (const auto &b : board.shapes()) {
            if (a != b && a->rect().intersects(b->rect()))
                return true;
        }
    }
    return false;
}

} // namespace

class TestDiagramDataModel : public QObject {
    Q_OBJECT

private slots:

    void registersTheFamily()
    {
        const Family *family = findFamily(QStringLiteral("datamodel"));
        QVERIFY(family);
        QCOMPARE(family->title, QStringLiteral("Data model"));
        QCOMPARE(codec().language(), QStringLiteral("mermaid"));
        QCOMPARE(&codec().family(), family);
        QCOMPARE(codecFor(QStringLiteral("mermaid"), QString::fromUtf8(kFixture)), &codec());
        QCOMPARE(defaultCodec(*family), &codec());
        QVERIFY(!codec().matches(QStringLiteral("flowchart TB\n  A --> B")));
        QVERIFY(codec().matches(QStringLiteral("%% a comment\nerDiagram\n")));
    }

    // diagram-families.test.js: the registry, the model, the round trip.
    void parsesAndSerializesTheFixture()
    {
        const Model model = parse(QString::fromUtf8(kFixture));
        QCOMPARE(model.nodes.size(), 2);
        QCOMPARE(model.links.size(), 1);
        QVERIFY(model.warnings.isEmpty());
        QVERIFY(jsonStrings(model.nodes[0].props, QStringLiteral("attributes"))
                    .contains(QStringLiteral("string email UK \"Adresse de connexion\"")));
        QVERIFY(jsonStrings(model.nodes[1].props, QStringLiteral("attributes"))
                    .contains(QStringLiteral("uuid customer_id FK")));
        QCOMPARE(model.meta.value(QStringLiteral("rankdir")).toString(), QStringLiteral("LR"));

        Board board;
        buildBoard(board, codec(), model);
        const QString serialized = codec().serialize(board, model.meta);
        QVERIFY(serialized.startsWith(QStringLiteral("erDiagram")));
        QCOMPARE(parse(serialized).nodes.size(), 2);
        QCOMPARE(serialized, QStringLiteral("erDiagram\n  CUSTOMER {\n    uuid id PK\n"
                                            "    string email UK \"Adresse de connexion\"\n  }\n"
                                            "  ORDER {\n    uuid id PK\n    uuid customer_id FK\n  }\n"
                                            "  CUSTOMER ||--o{ ORDER : places"));
    }

    // A table named in the user's language stays a table.
    void readsUnicodeTableNames()
    {
        const Model model = parse(QStringLiteral("erDiagram\n    ÉTABLISSEMENT ||--o{ CLIENT : héberge\n"
                                                 "    CLIENT ||--o{ COMMANDE : passe\n    ÉTABLISSEMENT {\n"
                                                 "        uuid id PK\n        string nom\n    }"));
        QStringList keys;
        for (const Model::Node &node : model.nodes)
            keys.append(node.key);
        QCOMPARE(keys,
                 (QStringList{QStringLiteral("ÉTABLISSEMENT"), QStringLiteral("CLIENT"), QStringLiteral("COMMANDE")}));
        QCOMPARE(jsonStrings(model.nodes[0].props, QStringLiteral("attributes")),
                 (QStringList{QStringLiteral("uuid id PK"), QStringLiteral("string nom")}));
        QStringList links;
        for (const Model::Edge &edge : model.links)
            links.append(edge.from + QStringLiteral("->") + edge.to);
        QCOMPARE(links, (QStringList{QStringLiteral("ÉTABLISSEMENT->CLIENT"), QStringLiteral("CLIENT->COMMANDE")}));
        QCOMPARE(model.links[0].props.value(QStringLiteral("label")).toString(), QStringLiteral("héberge"));
    }

    void readsTheRestOfTheGrammar()
    {
        const Model model = parse(QStringLiteral("erDiagram\n  CUSTOMER[\"Customer account\"] {\n    uuid id PK\n  }\n"
                                                 "  EMPTY {}\n  ALONE\n  ORDER { uuid id PK }\n"
                                                 "  CUSTOMER |o..|{ ORDER : \"may place\"\n  this is nonsense\n"));
        QCOMPARE(model.nodes.size(), 4);
        QCOMPARE(model.nodes[0].props.value(QStringLiteral("name")).toString(), QStringLiteral("Customer account"));
        QCOMPARE(jsonStrings(model.nodes[3].props, QStringLiteral("attributes")),
                 QStringList{QStringLiteral("uuid id PK")});
        QCOMPARE(model.links.size(), 1);
        const QJsonObject link = model.links[0].props;
        QCOMPARE(link.value(QStringLiteral("fromCard")).toString(), QStringLiteral("|o"));
        QCOMPARE(link.value(QStringLiteral("toCard")).toString(), QStringLiteral("|{"));
        QCOMPARE(link.value(QStringLiteral("identifying")).toBool(true), false);
        QCOMPARE(link.value(QStringLiteral("label")).toString(), QStringLiteral("may place"));
        QCOMPARE(model.warnings, QStringList{QStringLiteral("Not understood: this is nonsense")});

        Board board;
        buildBoard(board, codec(), model);
        const QString serialized = codec().serialize(board, model.meta);
        QVERIFY(serialized.contains(QStringLiteral("  CUSTOMER[\"Customer account\"] {")));
        QVERIFY(serialized.contains(QStringLiteral("  CUSTOMER |o..|{ ORDER : \"may place\"")));
        const Model again = parse(serialized);
        QVERIFY(again.warnings.isEmpty());
        QCOMPARE(again.nodes.size(), 4);
        QCOMPARE(again.links[0].props.value(QStringLiteral("identifying")).toBool(true), false);
    }

    // The tables are real tables with structured fields.
    void measuresStructuredFields()
    {
        const Field id = parseDatabaseField(QStringLiteral("uuid id PK"));
        QCOMPARE(id.raw, QStringLiteral("uuid id PK"));
        QCOMPARE(id.type, QStringLiteral("uuid"));
        QCOMPARE(id.name, QStringLiteral("id"));
        QCOMPARE(id.constraints, QStringList{QStringLiteral("PK")});
        QCOMPARE(id.comment, QString());
        const Field keys = parseDatabaseField(QStringLiteral("int owner_id uk, fk,PK 'the owner'"));
        QCOMPARE(keys.constraints, (QStringList{QStringLiteral("PK"), QStringLiteral("FK"), QStringLiteral("UK")}));
        QCOMPARE(keys.comment, QStringLiteral("the owner"));
        QCOMPARE(databaseFieldText(keys), QStringLiteral("int owner_id PK, FK, UK \"the owner\""));
        QCOMPARE(parseDatabaseField(QStringLiteral("lonely")).name, QStringLiteral("lonely"));

        Board board;
        board.setFamily(&families::dataModel());
        const EntityShape *table =
            addTable(board, {.id = QStringLiteral("t"),
                             .position = {0, 0},
                             .name = QStringLiteral("CUSTOMER"),
                             .attributes = {QStringLiteral("uuid id PK"),
                                            QStringLiteral("varchar(255) email UK \"Adresse de connexion\""),
                                            QStringLiteral("uuid account_id FK")}});
        QVERIFY(table);
        const QList<Field> &fields = table->databaseFields();
        QCOMPARE(fields[1].constraints, QStringList{QStringLiteral("UK")});
        QCOMPARE(fields[1].comment, QStringLiteral("Adresse de connexion"));
        QCOMPARE(fields[2].constraints.front(), QStringLiteral("FK"));
        QVERIFY(table->w >= 310);
        QCOMPARE(table->h, 48 + 25 + 3 * 31 + 1.0);

        // A table without columns keeps one empty row.
        const EntityShape *empty = addTable(
            board,
            {.id = QStringLiteral("e"), .position = {0, 400}, .name = QStringLiteral("EMPTY"), .attributes = {}});
        QCOMPARE(empty->h, 48 + 25 + 31 + 1.0);
        QCOMPARE(empty->connectionPorts(board).size(), 4); // the four sides

        const RelationshipLink defaults;
        QCOMPARE(defaults.fromCard, QStringLiteral("||"));
        QCOMPARE(defaults.toCard, QStringLiteral("o{"));
    }

    // The relations join the PK and FK columns.
    void joinsTheKeyColumns()
    {
        const CustomerOrder f;
        QVERIFY(f.relation);
        const std::optional<RelationshipLink::ColumnEndpoints> ends = f.relation->columnEndpoints(f.board);
        QVERIFY(ends);
        QCOMPARE(ends->from.column, QStringLiteral("customer_id"));
        QCOMPARE(ends->to.column, QStringLiteral("id"));
        QCOMPARE(ends->fromShape, f.order);  // the FK column is the source
        QCOMPARE(ends->toShape, f.customer); // the head reaches the referenced table
        QCOMPARE(ends->from.point, *f.order->columnAnchor(QStringLiteral("customer_id"), true));
        QCOMPARE(ends->to.point, *f.customer->columnAnchor(QStringLiteral("id"), false));
        const std::optional<Route> route = f.relation->route(f.board);
        QVERIFY(route);
        QCOMPARE(route->points.size(), size_t(4)); // orthogonal between rows of different heights

        QStringList portKeys;
        for (const Port &port : f.customer->connectionPorts(f.board))
            portKeys.append(port.key);
        QCOMPARE(portKeys, (QStringList{QStringLiteral("id"), QStringLiteral("id"), QStringLiteral("email"),
                                        QStringLiteral("email")}));
        const std::optional<Port> port =
            f.order->connectionPortAt(f.board, QPointF(f.order->x + 5, f.order->y + 48 + 25 + 31 + 10));
        QVERIFY(port);
        QCOMPARE(port->key, QStringLiteral("customer_id"));
        QCOMPARE(port->x, f.order->x);

        const QString table =
            f.customer->graphvizNodeAttributes(f.board, QStringLiteral("LR"), f.customer->w, f.customer->h).join(' ');
        QVERIFY(table.contains(QStringLiteral("shape=plain")));
        QVERIFY(table.contains(QStringLiteral("FIXEDSIZE=\"TRUE\" WIDTH=\"%1\" HEIGHT=\"%2\"")
                                   .arg(std::ceil(f.customer->w))
                                   .arg(std::ceil(f.customer->h))));
        QVERIFY(table.contains(QStringLiteral("PORT=\"column_0\"")));
        QVERIFY(table.contains(QStringLiteral("PORT=\"column_1\"")));

        const auto graphvizEnds = f.relation->graphvizEndpoints(f.board, QStringLiteral("LR"));
        QVERIFY(graphvizEnds);
        QCOMPARE(graphvizEnds->first.id, QStringLiteral("order"));
        QCOMPARE(graphvizEnds->first.port, QStringLiteral("column_1"));
        QCOMPARE(graphvizEnds->first.compass, QStringLiteral("e"));
        QCOMPARE(graphvizEnds->second.id, QStringLiteral("customer"));
        QCOMPARE(graphvizEnds->second.port, QStringLiteral("column_0"));
        QCOMPARE(graphvizEnds->second.compass, QStringLiteral("w"));
        QCOMPARE(f.relation->graphvizEndpoints(f.board, QStringLiteral("RL"))->first.compass, QStringLiteral("w"));

        // A physical schema reserves no label on an FK, but keeps it in the
        // source.
        QCOMPARE(f.relation->layoutLabel(f.board), QString());
        QCOMPARE(f.relation->toJson().value(QStringLiteral("label")).toString(), QStringLiteral("passe"));

        // The joined columns are marked on their tables.
        const auto related = f.customer->relatedColumns(f.board);
        QCOMPARE(related.keys(), QStringList{QStringLiteral("id")});
        QCOMPARE(related.value(QStringLiteral("id")), QSet<bool>{false}); // the right side

        // A Graphviz spline anchored on the columns is kept.
        Spline spline;
        spline.points = {ends->from.point, ends->from.point + QPointF(-60, 0), ends->to.point + QPointF(60, 0),
                         ends->to.point + QPointF(9, 0)};
        spline.end = ends->to.point;
        spline.labelPoint = QPointF(250, 42);
        spline.fromAnchor = QPointF(f.customer->x, f.customer->y);
        spline.toAnchor = QPointF(f.order->x, f.order->y);
        f.relation->spline = spline;
        const std::optional<Route> curved = f.relation->route(f.board);
        QVERIFY(curved);
        QCOMPARE(curved->curve, spline.points);
        QVERIFY(!curved->labelPoint); // no label place off its curve
        // Moved: back to the orthogonal route.
        f.order->x += 40;
        QVERIFY(f.relation->route(f.board)->curve.empty());
    }

    void routesThroughTheColumnsWithGraphviz()
    {
        CustomerOrder f;
        const QPointF customer(f.customer->x, f.customer->y);
        const QPointF order(f.order->x, f.order->y);
        QVERIFY(graphviz::routeLinks(f.board));
        QCOMPARE(QPointF(f.customer->x, f.customer->y), customer);
        QCOMPARE(QPointF(f.order->x, f.order->y), order);
        const std::optional<Route> route = f.relation->route(f.board);
        QVERIFY(route);
        QVERIFY(!route->curve.empty()); // the spline anchored on the column rows
        const auto ends = f.relation->columnEndpoints(f.board);
        QVERIFY(QLineF(route->a, ends->from.point).length() <= 2);
        QVERIFY(QLineF(route->b, ends->to.point).length() <= 2);
    }

    // A relation created by hand keeps the columns chosen.
    void keepsTheColumnsChosenByHand()
    {
        Board board;
        board.setFamily(&families::dataModel());
        const EntityShape *source =
            addTable(board, {.id = QStringLiteral("source"),
                             .position = {0, 0},
                             .name = QStringLiteral("SOURCE"),
                             .attributes = {QStringLiteral("uuid id PK"), QStringLiteral("uuid owner_id FK")}});
        const EntityShape *target =
            addTable(board, {.id = QStringLiteral("target"),
                             .position = {500, 0},
                             .name = QStringLiteral("TARGET"),
                             .attributes = {QStringLiteral("uuid id PK"), QStringLiteral("uuid public_id UK")}});
        const auto *relation = dynamic_cast<RelationshipLink *>(
            board.addLink(QStringLiteral("relationship"), source->id, target->id,
                          QJsonObject{{QStringLiteral("fromPort"), QStringLiteral("owner_id")},
                                      {QStringLiteral("toPort"), QStringLiteral("public_id")}}));
        const RelationshipLink::Endpoints ends = relation->relationshipEndpoints(*source, *target);
        QCOMPARE(ends.fromColumn, QStringLiteral("owner_id"));
        QCOMPARE(ends.toColumn, QStringLiteral("public_id"));
        QCOMPARE(relation->toJson().value(QStringLiteral("fromColumn")).toString(), QStringLiteral("owner_id"));
        QCOMPARE(relation->toJson().value(QStringLiteral("toColumn")).toString(), QStringLiteral("public_id"));

        // The FK comment names the referenced column.
        EntityShape *line =
            addTable(board, {.id = QStringLiteral("line"),
                             .position = {0, 300},
                             .name = QStringLiteral("order_lines"),
                             .attributes = {QStringLiteral("uuid id PK"),
                                            QStringLiteral("uuid product FK \"references target.public_id\"")}});
        const auto *reference =
            dynamic_cast<RelationshipLink *>(board.addLink(QStringLiteral("relationship"), target->id, line->id, {}));
        const RelationshipLink::Endpoints referenced = reference->relationshipEndpoints(*target, *line);
        QCOMPARE(referenced.fromShape, line);
        QCOMPARE(referenced.fromColumn, QStringLiteral("product"));
        QCOMPARE(referenced.toColumn, QStringLiteral("public_id"));
    }

    void exposesItsProperties()
    {
        const CustomerOrder f;
        QStringList ids;
        for (const Property &p : f.relation->properties())
            ids.append(p.id);
        QCOMPARE(ids,
                 (QStringList{QStringLiteral("fromCard"), QStringLiteral("toCard"), QStringLiteral("identifying")}));
        f.relation->setPropertyValue(QStringLiteral("toCard"), QStringLiteral("|{"));
        f.relation->setPropertyValue(QStringLiteral("identifying"), false);
        QCOMPARE(f.relation->propertyValue(QStringLiteral("toCard")).toString(), QStringLiteral("|{"));
        QVERIFY(!f.relation->toJson().value(QStringLiteral("identifying")).toBool(true));

        f.customer->setPropertyValue(QStringLiteral("attributes"), QStringLiteral("uuid id PK\n\nint age"));
        QCOMPARE(f.customer->attributes, (QStringList{QStringLiteral("uuid id PK"), QStringLiteral("int age")}));
        f.customer->setPropertyValue(QStringLiteral("name"), QStringLiteral("CLIENT"));
        QCOMPARE(f.customer->propertyValue(QStringLiteral("name")).toString(), QStringLiteral("CLIENT"));
        const QString serialized = codec().serialize(f.board, {});
        // No key yet: the name gives it.
        QVERIFY(serialized.contains(QStringLiteral("  CLIENT {\n    uuid id PK\n    int age\n  }")));
        QVERIFY(serialized.contains(QStringLiteral("  CLIENT ||..|{ ORDER : passe")));
    }

    void savesAndLoadsDocuments()
    {
        // The blank diagram.
        Board blank;
        document::Loaded loaded;
        QVERIFY(loadInto(blank, document::blank(QStringLiteral("datamodel")), &loaded));
        QCOMPARE(loaded.codec, &codec());
        QCOMPARE(blank.shapes().size(), size_t(2));
        QCOMPARE(blank.links().size(), size_t(1));
        QVERIFY(loaded.warnings.isEmpty());

        // A richer one, laid out, saved and read back.
        const QByteArray source = "erDiagram\n  CUSTOMER {\n    uuid id PK\n    string email UK \"login\"\n  }\n"
                                  "  ORDER {\n    uuid id PK\n    uuid customer_id FK\n    decimal total\n  }\n"
                                  "  LINE {\n    uuid order_id PK, FK\n    uuid product_id FK\n  }\n"
                                  "  PRODUCT {\n    uuid id PK\n    string label\n  }\n"
                                  "  CUSTOMER ||--o{ ORDER : places\n  ORDER ||--|{ LINE : contains\n"
                                  "  PRODUCT |o..o{ LINE : \"is sold in\"\n";
        Board board;
        QVERIFY(loadInto(board, source, &loaded));
        QVERIFY(loaded.needsArrange);
        graphviz::ArrangeOptions options;
        options.rankdir = board.meta.value(QStringLiteral("rankdir")).toString();
        QCOMPARE(options.rankdir, QStringLiteral("LR"));
        QVERIFY(graphviz::autoArrange(board, options));
        QVERIFY(!overlaps(board));
        for (const auto &l : board.links())
            QVERIFY(l->spline);

        // The relations still join their columns after the layout.
        for (const auto &l : board.links()) {
            const auto *relation = dynamic_cast<const RelationshipLink *>(l.get());
            const auto ends = relation->columnEndpoints(board);
            QVERIFY(ends);
            const std::optional<Route> route = relation->route(board);
            QVERIFY(QLineF(route->a, ends->from.point).length() <= 2);
            QVERIFY(QLineF(route->b, ends->to.point).length() <= 2);
        }

        const QByteArray saved = document::save(board, codec());
        Board again;
        QVERIFY(loadInto(again, saved, &loaded));
        QVERIFY(!loaded.needsArrange);
        QCOMPARE(again.shapes().size(), size_t(4));
        QCOMPARE(again.links().size(), size_t(3));
        for (const auto &s : board.shapes()) {
            const auto *table = dynamic_cast<const EntityShape *>(s.get());
            const auto *read = dynamic_cast<const EntityShape *>(byKey(again, s->key));
            QVERIFY(read);
            QCOMPARE(read->name, table->name);
            QCOMPARE(read->attributes, table->attributes);
            QVERIFY(std::abs(read->x - s->x) < 0.1);
            QVERIFY(std::abs(read->y - s->y) < 0.1);
        }
        QStringList relations;
        for (const auto &l : again.links()) {
            const auto *relation = dynamic_cast<const RelationshipLink *>(l.get());
            relations.append(QStringLiteral("%1 %2 %3 %4 %5 %6")
                                 .arg(again.shape(l->from)->key, relation->fromCard, relation->toCard,
                                      again.shape(l->to)->key, l->label)
                                 .arg(relation->identifying ? QStringLiteral("true") : QStringLiteral("false")));
            QVERIFY(l->spline);
        }
        QCOMPARE(relations, (QStringList{QStringLiteral("CUSTOMER || o{ ORDER places true"),
                                         QStringLiteral("ORDER || |{ LINE contains true"),
                                         QStringLiteral("PRODUCT |o o{ LINE is sold in false")}));
        QCOMPARE(document::source(again, codec()), document::source(board, codec()));

        // A table renamed keeps its key and its place: the new name is an
        // alias.
        dynamic_cast<EntityShape *>(byKey(again, QStringLiteral("CUSTOMER")))->name = QStringLiteral("Client");
        const QByteArray renamed = document::save(again, codec());
        QVERIFY(renamed.contains("  CUSTOMER[\"Client\"] {"));
        Board third;
        QVERIFY(loadInto(third, renamed, &loaded));
        QVERIFY(!loaded.needsArrange);
        QCOMPARE(dynamic_cast<EntityShape *>(byKey(third, QStringLiteral("CUSTOMER")))->name, QStringLiteral("Client"));

        // Undo and redo rebuild the shapes from their JSON.
        const QString before = document::source(third, codec());
        third.edit(
            [&] { dynamic_cast<EntityShape *>(byKey(third, QStringLiteral("ORDER")))->attributes.append("int n"); });
        QVERIFY(document::source(third, codec()) != before);
        third.undo();
        QCOMPARE(document::source(third, codec()), before);
    }

    void drawsWithoutCrashing()
    {
        CustomerOrder f;
        addTable(
            f.board,
            {.id = QStringLiteral("empty"), .position = {0, 400}, .name = QStringLiteral("EMPTY"), .attributes = {}});
        f.customer->customFill = QStringLiteral("#ffcdd2");
        f.board.selectShape(f.order->id);
        f.board.selectLink(f.relation->id, true);
        QImage image(1200, 800, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        Canvas::paintBoard(painter, f.board);
        painter.end();
        QVERIFY(!image.isNull());
    }
};

QTEST_MAIN(TestDiagramDataModel)
#include "tst_diagram_datamodel.moc"
