// SPDX-License-Identifier: GPL-3.0-only

// UML class diagrams: the classifier box, the relations, and the Mermaid and
// PlantUML codecs (ported from sidecar's class-diagram.test.js).

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Canvas.h"
#include "viewers/diagram/Document.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/Graphviz.h"
#include "viewers/diagram/families/Families.h"
#include "viewers/diagram/families/classes/Classes.h"

#include <QImage>
#include <QJsonArray>
#include <QPainter>
#include <QtTest>

#include <optional>

using namespace diagram;
using namespace diagram::classes;

namespace {

const Codec &mermaid()
{
    return *families::classCodecs().at(0);
}

const Codec &plantuml()
{
    return *families::classCodecs().at(1);
}

const Model::Node *node(const Model &model, const QString &key)
{
    for (const Model::Node &n : model.nodes) {
        if (n.key == key)
            return &n;
    }
    return nullptr;
}

const Model::Edge *edgeOfKind(const Model &model, const QString &kind)
{
    for (const Model::Edge &e : model.links) {
        if (e.props.value(QStringLiteral("kind")).toString() == kind)
            return &e;
    }
    return nullptr;
}

QStringList kinds(const Model &model)
{
    QStringList result;
    for (const Model::Edge &e : model.links)
        result.append(e.props.value(QStringLiteral("kind")).toString());
    result.sort();
    return result;
}

QStringList relationKinds()
{
    QStringList result;
    for (const RelationPreset &preset : families::classes().relations)
        result.append(preset.props.value(QStringLiteral("kind")).toString());
    result.sort();
    return result;
}

ClassShape *byKey(const Board &board, const QString &key)
{
    for (const auto &s : board.shapes()) {
        if (s->key == key)
            return dynamic_cast<ClassShape *>(s.get());
    }
    return nullptr;
}

bool loadInto(Board &board, const QString &name, const QByteArray &content, document::Loaded *loaded = nullptr)
{
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

graphviz::ArrangeOptions options(const Board &board)
{
    const Family &f = *board.family();
    graphviz::ArrangeOptions o;
    o.rankdir = board.meta.value(QStringLiteral("rankdir")).toString(QStringLiteral("TB"));
    o.clusters = f.graphvizClusters;
    o.splines = f.graphvizSplines;
    o.inferClusterMembership = f.inferClusterMembership;
    o.stackDisconnected = f.graphvizStackDisconnected;
    o.adaptiveDirection = f.graphvizAdaptiveDirection;
    return o;
}

const char *const kAllMarkers = R"(classDiagram
    class Base
    class Child {
      <<abstract>>
      -id: String
      +run(): void
    }
    class Contract {
      <<interface>>
    }
    class Part
    class Whole
    class Client
    class Service
    Child --|> Base : inherits
    Service ..|> Contract : implements
    Part "0..*" --* "1" Whole : composes
    Part "0..*" --o "1" Whole : aggregates
    Client --> Service : calls
    Client ..> Contract : depends
    Client -- Service : associated
    Client .. Service : dotted)";

} // namespace

class TestDiagramClasses : public QObject {
    Q_OBJECT

private slots:

    void exposesTheUmlClassifiersAndRelations()
    {
        const Family &f = families::classes();
        QCOMPARE(f.id, QStringLiteral("classes"));
        QCOMPARE(f.title, QStringLiteral("Class diagram"));
        QVERIFY(f.cardinalities);
        QVERIFY(f.links.front().allowMulti);
        QStringList ids;
        for (const ElementPreset &preset : f.elements)
            ids.append(preset.id);
        QCOMPARE(ids, (QStringList{"class", "abstract", "interface", "enumeration", "datatype", "primitive", "signal",
                                   "metaclass", "annotation", "entity", "struct", "exception", "protocol", "custom"}));
        QCOMPARE(f.elements.front().props.value(QStringLiteral("name")).toString(), QStringLiteral("Class"));
        QVERIFY(f.elements.front().props.value(QStringLiteral("stereotype")).isNull());
        QCOMPARE(f.elements.at(2).props.value(QStringLiteral("stereotype")).toString(), QStringLiteral("interface"));
        QCOMPARE(relationKinds(), (QStringList{"aggregation", "association", "composition", "dependency", "dotted",
                                               "extension", "link", "realization"}));
        for (const RelationPreset &preset : f.relations) {
            const QString label = preset.props.value(QStringLiteral("label")).toString();
            QVERIFY(!label.isEmpty());
            QCOMPARE(label, defaultRelationLabel(preset.props.value(QStringLiteral("kind")).toString()));
        }
        QCOMPARE(defaultRelationLabel(QStringLiteral("extension")), QStringLiteral("Inheritance"));
        QCOMPARE(defaultRelationLabel(QStringLiteral("association")), QStringLiteral("Directed association"));
        QCOMPARE(defaultRelationLabel(QStringLiteral("unknown")), QStringLiteral("Relationship"));
    }

    void givesEachRelationItsDefaultLabelWithoutOverridingOne()
    {
        for (const QString &kind : relationKinds()) {
            ClassLink link;
            link.load(QJsonObject{{"from", "a"}, {"to", "b"}, {"kind", kind}});
            QCOMPARE(link.label, defaultRelationLabel(kind));
        }
        ClassLink uses;
        uses.load(QJsonObject{{"from", "a"}, {"to", "b"}, {"kind", "dependency"}, {"label", " uses "}});
        QCOMPARE(uses.label, QStringLiteral("uses"));
        ClassLink empty;
        empty.load(QJsonObject{{"from", "a"}, {"to", "b"}, {"kind", "dependency"}, {"label", ""}});
        QVERIFY(empty.label.isEmpty());
        ClassLink fallback;
        fallback.load(QJsonObject{{"from", "a"}, {"to", "b"}});
        QCOMPARE(fallback.kind, QStringLiteral("association"));
    }

    void keepsTheJsonSemantics()
    {
        ClassShape classifier;
        classifier.load(QJsonObject{{"id", "service"},
                                    {"name", "Service"},
                                    {"stereotype", "abstract"},
                                    {"attributes", QJsonArray{"-repository: Repository"}},
                                    {"methods", QJsonArray{"+execute(): Result"}}});
        const QJsonObject json = classifier.toJson();
        QCOMPARE(json.value("type").toString(), QStringLiteral("class"));
        QCOMPARE(json.value("w").toDouble(), 180.0);
        QCOMPARE(json.value("h").toDouble(), 60.0);
        QCOMPARE(json.value("name").toString(), QStringLiteral("Service"));
        QCOMPARE(json.value("stereotype").toString(), QStringLiteral("abstract"));
        QCOMPARE(json.value("attributes").toArray(), QJsonArray{"-repository: Repository"});
        QCOMPARE(json.value("methods").toArray(), QJsonArray{"+execute(): Result"});
        QVERIFY(!classifier.showConnectionPorts());

        ClassShape plain;
        plain.load(QJsonObject{});
        QCOMPARE(plain.name, QStringLiteral("Class"));
        QVERIFY(plain.toJson().value("stereotype").isNull());
        ClassShape fromText;
        fromText.load(QJsonObject{{"text", "Order"}});
        QCOMPARE(fromText.name, QStringLiteral("Order"));

        ClassLink relation;
        relation.load(QJsonObject{{"id", "owns"},
                                  {"from", "service"},
                                  {"to", "repository"},
                                  {"kind", "composition"},
                                  {"label", "owns"},
                                  {"fromCard", "1"},
                                  {"toCard", "0..*"}});
        const QJsonObject link = relation.toJson();
        QCOMPARE(link.value("kind").toString(), QStringLiteral("composition"));
        QCOMPARE(link.value("fromCard").toString(), QStringLiteral("1"));
        QCOMPARE(link.value("toCard").toString(), QStringLiteral("0..*"));
        QCOMPARE(link.value("label").toString(), QStringLiteral("owns"));
    }

    void followsItsLongestMember()
    {
        Board board;
        board.setFamily(&families::classes());
        Shape *s =
            board.addShape(QStringLiteral("class"), QJsonObject{{"name", "Service"},
                                                                {"attributes", QJsonArray{"-repository: Repository"}},
                                                                {"methods", QJsonArray{"+execute(): Result"}}});
        auto *c = dynamic_cast<ClassShape *>(s);
        QVERIFY(c);
        // Header (1 line) + two sections of one member each.
        QCOMPARE(c->h, 116.0);
        const qreal shortWidth = c->w;

        c->methods = {QStringLiteral("+runABusinessOperationWithAVeryDetailedName(argument: ComplexType): Result")};
        board.measureShape(*c);
        QVERIFY(c->w > shortWidth);
        QVERIFY(c->w >= text::width(Font(12.5), c->methods.front()) + 28);

        c->attributes.clear();
        c->methods.clear();
        board.measureShape(*c);
        QCOMPARE(c->w, 150.0);
        QCOMPARE(c->h, 48.0 + 14 + 14);

        c->stereotype = QStringLiteral("interface");
        board.measureShape(*c);
        QCOMPARE(c->h, 68.0 + 14 + 14);
    }

    void editsTheWholeClassAsText()
    {
        ClassShape c;
        c.load(QJsonObject{{"name", "Service"},
                           {"stereotype", "interface"},
                           {"attributes", QJsonArray{"-id: UUID"}},
                           {"methods", QJsonArray{"+run()", "+stop()"}}});
        const QString text = c.editText();
        QCOMPARE(text, QStringLiteral("<<interface>>\nService\n--\n-id: UUID\n--\n+run()\n+stop()"));
        c.setEditText(QStringLiteral("Renamed\n--\n  -a: int\n\n-b: int\n--\n+go()\n--\n+more()"));
        QVERIFY(c.stereotype.isEmpty());
        QCOMPARE(c.name, QStringLiteral("Renamed"));
        QCOMPARE(c.attributes, (QStringList{"-a: int", "-b: int"}));
        // A third separator stays in the methods.
        QCOMPARE(c.methods, (QStringList{"+go()", "+more()"}));
        c.setEditText(QStringLiteral("<<enumeration>>\n--\nRED"));
        QCOMPARE(c.name, QStringLiteral("Class"));
        QCOMPARE(c.stereotype, QStringLiteral("enumeration"));
        QCOMPARE(c.attributes, QStringList{"RED"});
        c.setEditText(text);
        QCOMPARE(c.editText(), text);

        // Enter is a new line: the members are several lines.
        Board board;
        board.setFamily(&families::classes());
        Shape *s = board.addShape(QStringLiteral("class"), QJsonObject{{"name", "A"}});
        bool multiline = false;
        board.setTextEditor([&](Shape *, Link *, const TextEditSpec &spec) { multiline = !spec.enterCommits; });
        s->onDoubleClick(board);
        QVERIFY(multiline);
    }

    void exposesStereotypeKindAndCardinalities()
    {
        ClassShape c;
        c.load(QJsonObject{{"name", "Order"}, {"stereotype", "service"}});
        const QList<Property> properties = c.properties();
        QCOMPARE(properties.size(), 1);
        QCOMPARE(properties.front().id, QStringLiteral("stereotype"));
        QStringList values;
        for (const auto &[value, label] : properties.front().choices)
            values.append(value);
        QVERIFY(values.contains(QString()));
        QVERIFY(values.contains(QStringLiteral("interface")));
        QVERIFY(values.contains(QStringLiteral("abstract")));
        QVERIFY(values.contains(QStringLiteral("service"))); // the source's own stays selectable
        c.setPropertyValue(QStringLiteral("stereotype"), QString());
        QVERIFY(c.stereotype.isEmpty());

        ClassLink l;
        l.load(QJsonObject{{"from", "a"}, {"to", "b"}, {"kind", "association"}});
        QStringList ids;
        for (const Property &p : l.properties())
            ids.append(p.id);
        QCOMPARE(ids, (QStringList{"kind", "fromCard", "toCard"}));
        l.setPropertyValue(QStringLiteral("kind"), QStringLiteral("extension"));
        QCOMPARE(l.kind, QStringLiteral("extension"));
        QCOMPARE(l.label, QStringLiteral("Inheritance")); // the default label follows
        l.label = QStringLiteral("specializes");
        l.setPropertyValue(QStringLiteral("kind"), QStringLiteral("realization"));
        QCOMPARE(l.label, QStringLiteral("specializes")); // a label of the user's stays
        l.setPropertyValue(QStringLiteral("toCard"), QStringLiteral(" 1..* "));
        QCOMPARE(l.propertyValue(QStringLiteral("toCard")).toString(), QStringLiteral("1..*"));
    }

    void reservesTheMarkersInTheLayout()
    {
        Board board;
        board.setFamily(&families::classes());
        const auto attributesOf = [&](const char *kind) {
            ClassLink l;
            l.load(QJsonObject{{"kind", kind}});
            return l.graphvizAttributes(board);
        };
        QCOMPARE(attributesOf("aggregation"), QStringList{"arrowsize=1.800"});
        QCOMPARE(attributesOf("extension"), QStringList{"arrowsize=1.400"});
        QCOMPARE(
            attributesOf("association"),
            QStringList{QStringLiteral("arrowsize=") + QString::number((board.style().link.arrow + 2) / 10, 'f', 3)});
        QCOMPARE(attributesOf("link"), QStringList{"arrowhead=none"});
        QCOMPARE(attributesOf("dotted"), QStringList{"arrowhead=none"});
    }

    void mermaidReloadsEveryMarkerAndCardinality()
    {
        Model model;
        QVERIFY(mermaid().matches(QString::fromUtf8(kAllMarkers)));
        QVERIFY(mermaid().parse(QString::fromUtf8(kAllMarkers), model, nullptr));
        QVERIFY(model.warnings.isEmpty());
        QCOMPARE(kinds(model), relationKinds());
        QCOMPARE(node(model, "Child")->props.value("stereotype").toString(), QStringLiteral("abstract"));
        QCOMPARE(node(model, "Child")->props.value("attributes").toArray(), QJsonArray{"-id: String"});
        QCOMPARE(node(model, "Child")->props.value("methods").toArray(), QJsonArray{"+run(): void"});
        QCOMPARE(node(model, "Contract")->props.value("stereotype").toString(), QStringLiteral("interface"));
        const Model::Edge *composition = edgeOfKind(model, "composition");
        QCOMPARE(composition->from, QStringLiteral("Part"));
        QCOMPARE(composition->to, QStringLiteral("Whole"));
        QCOMPARE(composition->props.value("fromCard").toString(), QStringLiteral("0..*"));
        QCOMPARE(composition->props.value("toCard").toString(), QStringLiteral("1"));
        QCOMPARE(edgeOfKind(model, "extension")->props.value("label").toString(), QStringLiteral("inherits"));

        Board board;
        buildBoard(board, mermaid(), model);
        QCOMPARE(board.links().size(), size_t(8)); // several relations between Client and Service
        const QString serialized = mermaid().serialize(board, board.meta);
        Model reloaded;
        QVERIFY(mermaid().parse(serialized, reloaded, nullptr));
        QVERIFY(reloaded.warnings.isEmpty());
        QCOMPARE(kinds(reloaded), relationKinds());
        QCOMPARE(edgeOfKind(reloaded, "composition")->props.value("fromCard").toString(), QStringLiteral("0..*"));
        QCOMPARE(edgeOfKind(reloaded, "composition")->props.value("toCard").toString(), QStringLiteral("1"));
        QCOMPARE(node(reloaded, "Child")->props.value("stereotype").toString(), QStringLiteral("abstract"));
        QCOMPARE(serialized, mermaid().serialize(board, board.meta));
        QVERIFY(serialized.contains(QStringLiteral("  Part \"0..*\" --* \"1\" Whole : composes")));
        QVERIFY(serialized.contains(QStringLiteral("  class Base\n")));
    }

    void mermaidNormalizesTheMarkerEnd()
    {
        Model model;
        QVERIFY(mermaid().parse(QStringLiteral("classDiagram\n  direction LR\n  Animal <|-- Dog\n"
                                               "  Car \"1\" *-- \"4\" Wheel : has\n  List~T~ <.. Client\n"
                                               "  class Box~T~[\"Box of T\"]\n  Box : +List~T~ items\n"),
                                model, nullptr));
        QCOMPARE(model.meta.value("rankdir").toString(), QStringLiteral("LR"));
        const Model::Edge *extension = edgeOfKind(model, "extension");
        QCOMPARE(extension->from, QStringLiteral("Dog"));
        QCOMPARE(extension->to, QStringLiteral("Animal"));
        const Model::Edge *composition = edgeOfKind(model, "composition");
        QCOMPARE(composition->from, QStringLiteral("Wheel"));
        QCOMPARE(composition->to, QStringLiteral("Car"));
        QCOMPARE(composition->props.value("fromCard").toString(), QStringLiteral("4"));
        QCOMPARE(composition->props.value("toCard").toString(), QStringLiteral("1"));
        const Model::Edge *dependency = edgeOfKind(model, "dependency");
        QCOMPARE(dependency->to, QStringLiteral("List"));
        QCOMPARE(node(model, "List")->props.value("name").toString(), QStringLiteral("List<T>"));
        QCOMPARE(node(model, "Box")->props.value("name").toString(), QStringLiteral("Box of T"));
        QCOMPARE(node(model, "Box")->props.value("attributes").toArray(), QJsonArray{"+List<T> items"});

        Board board;
        buildBoard(board, mermaid(), model);
        const QString out = mermaid().serialize(board, board.meta);
        QVERIFY(out.startsWith(QStringLiteral("classDiagram\n  direction LR\n")));
        QVERIFY(out.contains(QStringLiteral("    +List~T~ items")));
        QVERIFY(out.contains(QStringLiteral("  Wheel \"4\" --* \"1\" Car : has")));
    }

    void keepsClassesNamedInTheUsersLanguage()
    {
        Model model;
        QVERIFY(mermaid().parse(QStringLiteral("classDiagram\n    class Société {\n      +String nom\n    }\n"
                                               "    Société : +créer()\n    Utilisateur <|-- Société\n"
                                               "    <<interface>> Utilisateur"),
                                model, nullptr));
        QStringList keys;
        for (const Model::Node &n : model.nodes)
            keys.append(n.key);
        QCOMPARE(keys, (QStringList{"Société", "Utilisateur"}));
        QCOMPARE(node(model, "Société")->props.value("attributes").toArray(), QJsonArray{"+String nom"});
        QCOMPARE(node(model, "Société")->props.value("methods").toArray(), QJsonArray{"+créer()"});
        QCOMPARE(node(model, "Utilisateur")->props.value("stereotype").toString(), QStringLiteral("interface"));
        QCOMPARE(model.links.size(), 1);
        QCOMPARE(model.links.front().from, QStringLiteral("Société"));
        QCOMPARE(model.links.front().to, QStringLiteral("Utilisateur"));
        QCOMPARE(model.links.front().props.value("kind").toString(), QStringLiteral("extension"));
    }

    void reportsWhatItDoesNotUnderstand()
    {
        Model model;
        QVERIFY(
            mermaid().parse(QStringLiteral("classDiagram\n  class A\n  note \"for A\"\n  A ==> B\n"), model, nullptr));
        QCOMPARE(model.warnings, QStringList{"Not understood: A ==> B"});
        Model puml;
        QVERIFY(plantuml().parse(QStringLiteral("@startuml\nclass A\nA ~~ B\n@enduml"), puml, nullptr));
        QCOMPARE(puml.warnings, QStringList{"Not understood: A ~~ B"});
    }

    void plantumlReadsTheSpecializedDeclarations()
    {
        const QString source =
            QStringLiteral("@startuml\n    abstract class Base\n    interface Contract\n    enum Status\n"
                           "    annotation Injectable\n    entity Account\n    struct Value\n"
                           "    exception Failure\n    protocol Adapter\n    abstract Shape\n"
                           "    class Order <<aggregate>>\n@enduml");
        QVERIFY(plantuml().matches(source));
        QVERIFY(!mermaid().matches(source));
        Model model;
        QVERIFY(plantuml().parse(source, model, nullptr));
        const auto stereotype = [&](const char *key) { return node(model, key)->props.value("stereotype").toString(); };
        QCOMPARE(stereotype("Base"), QStringLiteral("abstract"));
        QCOMPARE(stereotype("Contract"), QStringLiteral("interface"));
        QCOMPARE(stereotype("Status"), QStringLiteral("enum"));
        QCOMPARE(stereotype("Injectable"), QStringLiteral("annotation"));
        QCOMPARE(stereotype("Account"), QStringLiteral("entity"));
        QCOMPARE(stereotype("Value"), QStringLiteral("struct"));
        QCOMPARE(stereotype("Failure"), QStringLiteral("exception"));
        QCOMPARE(stereotype("Adapter"), QStringLiteral("protocol"));
        QCOMPARE(stereotype("Shape"), QStringLiteral("abstract"));
        QCOMPARE(stereotype("Order"), QStringLiteral("aggregate"));
    }

    void plantumlRoundTrips()
    {
        const QString source = QStringLiteral("@startuml\n"
                                              "class \"Customer account\" as Account {\n"
                                              "  -id: UUID\n"
                                              "  {static} +create(): Account\n"
                                              "  --\n"
                                              "}\n"
                                              "class List<T>\n"
                                              "interface Repository\n"
                                              "Account \"1\" -left-> \"*\" Order : places >\n"
                                              "AccountRepository .[#red].|> Repository\n"
                                              "Order *--- Line\n"
                                              "Account : +close()\n"
                                              "List ..> Order\n"
                                              "@enduml");
        Model model;
        QVERIFY(plantuml().parse(source, model, nullptr));
        QVERIFY(model.warnings.isEmpty());
        const Model::Node *account = node(model, "Account");
        QCOMPARE(account->props.value("name").toString(), QStringLiteral("Customer account"));
        QCOMPARE(account->props.value("attributes").toArray(), QJsonArray{"-id: UUID"});
        QCOMPARE(account->props.value("methods").toArray(), (QJsonArray{"+create(): Account", "+close()"}));
        QCOMPARE(node(model, "List")->props.value("name").toString(), QStringLiteral("List<T>"));
        const Model::Edge *places = edgeOfKind(model, "association");
        QCOMPARE(places->from, QStringLiteral("Account"));
        QCOMPARE(places->to, QStringLiteral("Order"));
        QCOMPARE(places->props.value("label").toString(), QStringLiteral("places"));
        QCOMPARE(places->props.value("fromCard").toString(), QStringLiteral("1"));
        QCOMPARE(places->props.value("toCard").toString(), QStringLiteral("*"));
        QCOMPARE(edgeOfKind(model, "realization")->to, QStringLiteral("Repository"));
        QCOMPARE(edgeOfKind(model, "composition")->from, QStringLiteral("Line"));
        QCOMPARE(edgeOfKind(model, "composition")->to, QStringLiteral("Order"));

        Board board;
        buildBoard(board, plantuml(), model);
        const QString out = plantuml().serialize(board, board.meta);
        QVERIFY(out.startsWith(QStringLiteral("@startuml\nclass \"Customer account\" as Account {\n  -id: UUID\n")));
        QVERIFY(out.contains(QStringLiteral("\ninterface Repository\n")));
        QVERIFY(out.contains(QStringLiteral("\nAccount \"1\" --> \"*\" Order : places\n")));
        QVERIFY(out.endsWith(QStringLiteral("@enduml")));
        Model reloaded;
        QVERIFY(plantuml().parse(out, reloaded, nullptr));
        QVERIFY(reloaded.warnings.isEmpty());
        QCOMPARE(reloaded.nodes.size(), model.nodes.size());
        QCOMPARE(kinds(reloaded), kinds(model));
        Board again;
        buildBoard(again, plantuml(), reloaded);
        QCOMPARE(plantuml().serialize(again, again.meta), out);
    }

    void loadsAndSavesDocuments()
    {
        // The blank diagram of each notation.
        for (const auto &[name, codec] : {std::pair<const char *, const Codec *>{"a.mmd", &mermaid()},
                                          std::pair<const char *, const Codec *>{"a.puml", &plantuml()}}) {
            Board board;
            document::Loaded loaded;
            QVERIFY(loadInto(board, QString::fromLatin1(name), codec->blank().toUtf8(), &loaded));
            QCOMPARE(loaded.codec, codec);
            QVERIFY(loaded.warnings.isEmpty());
            QCOMPARE(board.shapes().size(), size_t(2));
            QCOMPARE(board.links().size(), size_t(1));
            QCOMPARE(byKey(board, "Service")->methods, QStringList{"+process()"});
            QCOMPARE(board.links().front()->label, QStringLiteral("uses"));
        }
        QCOMPARE(document::blank(QStringLiteral("classes")), mermaid().blank().toUtf8() + "\n");

        // A richer source: laid out, saved, read back unchanged.
        Board board;
        document::Loaded loaded;
        QVERIFY(loadInto(board, QStringLiteral("model.mmd"), kAllMarkers, &loaded));
        QVERIFY(loaded.needsArrange);
        QVERIFY(graphviz::autoArrange(board, options(board)));
        for (const auto &a : board.shapes()) {
            for (const auto &b : board.shapes()) {
                if (a != b)
                    QVERIFY2(!a->rect().intersects(b->rect()), qPrintable(a->key + QLatin1Char('/') + b->key));
            }
        }
        const QByteArray saved = document::save(board, *loaded.codec);
        QVERIFY(saved.contains("%% kodamiko {\"key\":\"Child\""));
        Board again;
        document::Loaded reloaded;
        QVERIFY(loadInto(again, QStringLiteral("model.mmd"), saved, &reloaded));
        QVERIFY(!reloaded.needsArrange);
        QCOMPARE(again.shapes().size(), board.shapes().size());
        QCOMPARE(again.links().size(), board.links().size());
        for (const auto &s : board.shapes()) {
            const auto *before = dynamic_cast<const ClassShape *>(s.get());
            const ClassShape *after = byKey(again, s->key);
            QVERIFY(after);
            QCOMPARE(after->name, before->name);
            QCOMPARE(after->stereotype, before->stereotype);
            QCOMPARE(after->attributes, before->attributes);
            QCOMPARE(after->methods, before->methods);
            QCOMPARE(after->x, std::round(before->x * 10) / 10);
            QCOMPARE(after->y, std::round(before->y * 10) / 10);
        }
        for (size_t i = 0; i < board.links().size(); ++i) {
            const auto *a = dynamic_cast<const ClassLink *>(board.links()[i].get());
            const auto *b = dynamic_cast<const ClassLink *>(again.links()[i].get());
            QCOMPARE(b->kind, a->kind);
            QCOMPARE(b->label, a->label);
            QCOMPARE(b->fromCard, a->fromCard);
            QCOMPARE(b->toCard, a->toCard);
            QCOMPARE(again.shape(b->from)->key, board.shape(a->from)->key);
            QCOMPARE(again.shape(b->to)->key, board.shape(a->to)->key);
        }

        // The same diagram in PlantUML.
        const QByteArray puml = document::save(board, plantuml());
        Board third;
        QVERIFY(loadInto(third, QStringLiteral("model.puml"), puml, &reloaded));
        QCOMPARE(reloaded.codec, &plantuml());
        QVERIFY(!reloaded.needsArrange);
        QCOMPARE(third.links().size(), board.links().size());
        QCOMPARE(byKey(third, "Child")->stereotype, QStringLiteral("abstract"));
    }

    void undoRestoresClassesAndRelations()
    {
        Board board;
        QVERIFY(loadInto(board, QStringLiteral("a.mmd"), kAllMarkers));
        const QByteArray before = board.snapshot();
        board.edit([&] {
            ClassShape *child = byKey(board, "Child");
            child->setEditText(QStringLiteral("Renamed\n--\n--\n+x()"));
            board.measureShape(*child);
        });
        QCOMPARE(byKey(board, "Child")->name, QStringLiteral("Renamed"));
        board.undo();
        QCOMPARE(board.snapshot(), before);
        QCOMPARE(byKey(board, "Child")->stereotype, QStringLiteral("abstract"));
    }

    void pointsAtTheClassFromItsTitleAndAtAMemberFromItsRow()
    {
        ClassShape classifier;
        classifier.load(QJsonObject{{"name", "AgentActivity"},
                                    {"attributes", QJsonArray{"-state: State"}},
                                    {"methods", QJsonArray{"+handle(event) void"}}});
        classifier.x = 0;
        classifier.y = 0;

        const auto title = classifier.codeReferenceAt(QPointF(40, 20));
        const auto attribute = classifier.codeReferenceAt(QPointF(40, 65));
        const auto method = classifier.codeReferenceAt(QPointF(40, 99));

        QVERIFY(title && attribute && method);
        QCOMPARE(title->name, QStringLiteral("AgentActivity"));
        QVERIFY(title->container.isEmpty());
        QCOMPARE(attribute->name, QStringLiteral("state"));
        QCOMPARE(attribute->container, QStringLiteral("AgentActivity"));
        QCOMPARE(method->name, QStringLiteral("handle"));
        QCOMPARE(method->container, QStringLiteral("AgentActivity"));
    }

    void readsOnlyANameAsACodeReference()
    {
        const auto qualified = codeReferenceIn(QStringLiteral("Git::run"));
        const auto generic = codeReferenceIn(QStringLiteral("List~T~"));

        QVERIFY(qualified && generic);
        QCOMPARE(qualified->name, QStringLiteral("run"));
        QCOMPARE(qualified->container, QStringLiteral("Git"));
        QCOMPARE(generic->name, QStringLiteral("List"));
        QVERIFY(!codeReferenceIn(QStringLiteral("Hook server")));
    }

    void readsMembersQualifiedWithADotAndNamesWrittenAsCalls()
    {
        const auto member = codeReferenceIn(QStringLiteral("Queue.enqueue"));
        const auto call = codeReferenceIn(QStringLiteral("enqueue(item)"));
        const auto linked = codeReferenceOf(QStringLiteral("src/queue.ts#Queue.enqueue"));

        QVERIFY(member && call && linked);
        QCOMPARE(member->name, QStringLiteral("enqueue"));
        QCOMPARE(member->container, QStringLiteral("Queue"));
        QCOMPARE(call->name, QStringLiteral("enqueue"));
        QVERIFY(call->container.isEmpty());
        QCOMPARE(linked->name, QStringLiteral("enqueue"));
        QCOMPARE(linked->container, QStringLiteral("Queue"));
        QCOMPARE(linked->filePath, QStringLiteral("src/queue.ts"));
    }

    void readsALinkToTheCodeAsASymbolAFileOrBoth()
    {
        const auto symbol = codeReferenceOf(QStringLiteral("Git::run"));
        const auto file = codeReferenceOf(QStringLiteral("src/git/Git.h"));
        const auto both = codeReferenceOf(QStringLiteral("src/git/Git.h#Git::run"));

        QVERIFY(symbol && file && both);
        QCOMPARE(symbol->name, QStringLiteral("run"));
        QCOMPARE(symbol->container, QStringLiteral("Git"));
        QVERIFY(symbol->filePath.isEmpty());
        QVERIFY(file->name.isEmpty());
        QCOMPARE(file->filePath, QStringLiteral("src/git/Git.h"));
        QCOMPARE(both->name, QStringLiteral("run"));
        QCOMPARE(both->filePath, QStringLiteral("src/git/Git.h"));
        QVERIFY(!codeReferenceOf(QStringLiteral("  ")));
    }

    void keepsTheMermaidLinksToTheCode()
    {
        Board board;
        QVERIFY(loadInto(board, QStringLiteral("a.mmd"),
                         "classDiagram\n  class Hook {\n    +listen()\n  }\n  class Other\n"
                         "  click Hook href \"src/agent/HookServer.h#HookServer\"\n  click Other call back()\n"));

        const ClassShape *hook = byKey(board, QStringLiteral("Hook"));
        const auto title = hook->codeReferenceAt(QPointF(hook->x + 20, hook->y + 20));
        const QByteArray saved = document::save(board, mermaid());

        QCOMPARE(hook->codeLink, QStringLiteral("src/agent/HookServer.h#HookServer"));
        QVERIFY(byKey(board, QStringLiteral("Other"))->codeLink.isEmpty());
        QVERIFY(title);
        QCOMPARE(title->name, QStringLiteral("HookServer"));
        QCOMPARE(title->filePath, QStringLiteral("src/agent/HookServer.h"));
        QVERIFY(saved.contains("\n  click Hook href \"src/agent/HookServer.h#HookServer\"\n"));
        QVERIFY(!saved.contains("click Other"));
    }

    void keepsThePlantumlLinksToTheCode()
    {
        Board board;
        QVERIFY(loadInto(board, QStringLiteral("a.puml"),
                         "@startuml\nclass Hook [[src/agent/HookServer.h#HookServer{the server}]] {\n  +listen()\n}\n"
                         "@enduml\n"));

        const ClassShape *hook = byKey(board, QStringLiteral("Hook"));
        const QByteArray saved = document::save(board, plantuml());

        QCOMPARE(hook->codeLink, QStringLiteral("src/agent/HookServer.h#HookServer"));
        QCOMPARE(hook->methods, QStringList{QStringLiteral("+listen()")});
        QVERIFY(saved.contains("class Hook [[src/agent/HookServer.h#HookServer]] {\n"));
    }

    void drawsWithoutCrashing()
    {
        Board board;
        QVERIFY(loadInto(board, QStringLiteral("a.mmd"), kAllMarkers));
        QVERIFY(graphviz::autoArrange(board, options(board)));
        board.selectShape(byKey(board, "Child")->id);
        QImage image(1200, 900, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        Canvas::paintBoard(painter, board);
        painter.end();
        // Relations drawn as straight lines too (no layout curve).
        for (const auto &l : board.links())
            l->spline.reset();
        QPainter again(&image);
        Canvas::paintBoard(again, board);
    }
};

QTEST_MAIN(TestDiagramClasses)
#include "tst_diagram_classes.moc"
