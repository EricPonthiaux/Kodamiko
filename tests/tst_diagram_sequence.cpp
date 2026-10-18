// The sequence diagram family, ported from sidecar's sequence-diagram.js and
// its Mermaid and PlantUML fences (test/sequence-diagram.test.js).

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Canvas.h"
#include "viewers/diagram/Document.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/Graphviz.h"
#include "viewers/diagram/families/Families.h"
#include "viewers/diagram/families/sequence/Sequence.h"

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QtTest>

#include <optional>

using namespace diagram;
using namespace diagram::sequence;

namespace {

const Codec &mermaid()
{
    return *families::sequenceCodecs().at(0);
}

const Codec &plantuml()
{
    return *families::sequenceCodecs().at(1);
}

Model parse(const Codec &codec, const QString &source)
{
    Model model;
    QString error;
    if (!codec.parse(source, model, &error))
        qWarning() << error;
    return model;
}

// A board built from a source, as a file load does before the layout.
void build(Board &board, const Codec &codec, const QString &source)
{
    buildBoard(board, codec, parse(codec, source));
}

QString roundTrip(const Codec &codec, const QString &source)
{
    Board board;
    build(board, codec, source);
    return codec.serialize(board, board.meta);
}

ParticipantShape *participant(Board &board, const QJsonObject &props)
{
    return dynamic_cast<ParticipantShape *>(board.addShape(QStringLiteral("participant"), props));
}

MessageLink *message(Board &board, const QString &from, const QString &to, const QJsonObject &props)
{
    return dynamic_cast<MessageLink *>(board.addLink(QStringLiteral("message"), from, to, props));
}

ParticipantShape *byKey(const Board &board, const QString &key)
{
    for (const auto &s : board.shapes()) {
        if (s->key == key)
            return dynamic_cast<ParticipantShape *>(s.get());
    }
    return nullptr;
}

QList<MessageLink *> messagesOf(const Board &board)
{
    QList<MessageLink *> result;
    for (const auto &l : board.links()) {
        if (auto *m = dynamic_cast<MessageLink *>(l.get()))
            result.append(m);
    }
    std::stable_sort(result.begin(), result.end(), [](const MessageLink *a, const MessageLink *b) {
        return a->order.value_or(0) < b->order.value_or(0);
    });
    return result;
}

bool load(Board &board, const QString &name, const QByteArray &content, document::Loaded *loaded = nullptr)
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

// What a file open does: the family's layout when elements have no place.
void loadArranged(Board &board, const QString &name, const QByteArray &content)
{
    document::Loaded loaded;
    QVERIFY(load(board, name, content, &loaded));
    if (loaded.needsArrange) {
        board.measureAll();
        board.family()->autoArrange(board);
    }
}

const char *const kRichMermaid = R"(sequenceDiagram
  %% A comment
  autonumber
  actor U as User
  participant W as Web app
  participant API
  participant DB as Database
  U->>W: Open the page
  W->>+API: GET /session
  loop Every minute
    API-)DB: Ping
  end
  alt Valid token
    API-->>-W: 200 OK
  else Expired token
    API--xW: 401
    W->>W: Refresh the token
  end
  par Load the profile
    W->>API: GET /profile
  and Load the news
    W->>API: GET /news
  end
  W-->>U: Show the page
)";

const char *const kRichPlantuml = R"(@startuml
actor User as U
participant "Web app" as W
database DB
queue Events
U -> W : open
W -> DB ++ : query
group Cache
  DB --> W -- : rows
  W ->> Events : published
else miss
  W ->x DB : lost
end
W <- DB : callback
W -> W : self
@enduml
)";

} // namespace

class TestDiagramSequence : public QObject {
    Q_OBJECT

private slots:

    void familyIsRegistered()
    {
        const Family *family = findFamily(QStringLiteral("sequence"));
        QVERIFY(family);
        QCOMPARE(family->defaultShape, QStringLiteral("participant"));
        QCOMPARE(family->defaultLink, QStringLiteral("message"));
        QVERIFY(family->links.first().allowSelf);
        QVERIFY(family->links.first().allowMulti);
        QVERIFY(family->autoArrange);
        QVERIFY(family->drawUnderlay);
        QCOMPARE(families::sequenceCodecs().size(), 2);
        QCOMPARE(mermaid().id(), QStringLiteral("mermaid-sequence"));
        QCOMPARE(plantuml().id(), QStringLiteral("plantuml-sequence"));
        QVERIFY(!mermaid().arrangeWithGraphviz());
        QVERIFY(!plantuml().arrangeWithGraphviz());
        QCOMPARE(codecFor(QStringLiteral("mermaid"), QStringLiteral("%% title\nsequenceDiagram\n  A->>B: hi")),
                 &mermaid());
        QCOMPARE(codecFor(QStringLiteral("plantuml"), QStringLiteral("@startuml\nA -> B : hi\n@enduml")), &plantuml());
        QVERIFY(!plantuml().matches(QStringLiteral("@startuml\nclass A\nA -> B\n@enduml")));
        QVERIFY(!plantuml().matches(QStringLiteral("@startuml\n[*] --> A\n@enduml")));
    }

    // The palette offers a participant and a real actor; both survive both
    // notations.
    void presetsOfferAParticipantAndAnActor()
    {
        const Family &family = families::sequence();
        QCOMPARE(family.elements.size(), 2);
        QCOMPARE(family.elements[0].props.value(QStringLiteral("kind")).toString(), QStringLiteral("participant"));
        QCOMPARE(family.elements[1].props.value(QStringLiteral("kind")).toString(), QStringLiteral("actor"));
        Board board;
        board.setFamily(&family);
        for (int i = 0; i < family.elements.size(); ++i) {
            QJsonObject props = family.elements[i].props;
            props.insert(QStringLiteral("key"), QStringLiteral("node%1").arg(i));
            props.insert(QStringLiteral("x"), i * 220);
            QVERIFY(board.addShape(family.elements[i].shapeType, props));
        }
        for (const Codec *codec : families::sequenceCodecs()) {
            const Model model = parse(*codec, codec->serialize(board, {}));
            QStringList kinds;
            for (const Model::Node &node : model.nodes)
                kinds.append(node.props.value(QStringLiteral("kind")).toString());
            QCOMPARE(kinds, (QStringList{QStringLiteral("participant"), QStringLiteral("actor")}));
        }
        const ParticipantShape *actor = participant(board, {{QStringLiteral("kind"), QStringLiteral("actor")},
                                                            {QStringLiteral("text"), QStringLiteral("Client")}});
        QVERIFY(actor->h >= 80);
    }

    void mermaidKeepsFragmentsBranchesActivationsAndDoubleHeads()
    {
        const QString source = QStringLiteral("sequenceDiagram\n"
                                              "    participant Client\n"
                                              "    participant API\n"
                                              "    alt Valid token\n"
                                              "      Client<<->>+API: Check\n"
                                              "    else Expired token\n"
                                              "      API-->>-Client: Refuse\n"
                                              "    end");
        const Model model = parse(mermaid(), source);
        QCOMPARE(model.links.size(), 2);
        const QJsonObject first = model.links[0].props;
        const QJsonObject second = model.links[1].props;
        QCOMPARE(first.value(QStringLiteral("startHead")).toString(), QStringLiteral("arrow"));
        QCOMPARE(first.value(QStringLiteral("activation")).toString(), QStringLiteral("+"));
        QCOMPARE(
            first.value(QStringLiteral("fragments")).toArray()[0].toObject().value(QStringLiteral("kind")).toString(),
            QStringLiteral("alt"));
        QCOMPARE(second.value(QStringLiteral("fragments"))
                     .toArray()[0]
                     .toObject()
                     .value(QStringLiteral("branchKeyword"))
                     .toString(),
                 QStringLiteral("else"));
        QVERIFY(model.warnings.isEmpty());

        const QString serialized = roundTrip(mermaid(), source);
        QVERIFY2(serialized.contains(QStringLiteral("alt Valid token")), qPrintable(serialized));
        QVERIFY(serialized.contains(QStringLiteral("else Expired token")));
        QVERIFY(serialized.contains(QStringLiteral("Client<<->>+API: Check")));
        QVERIFY(serialized.contains(QStringLiteral("API-->>-Client: Refuse")));
        QVERIFY(serialized.endsWith(QStringLiteral("\n  end")));

        const Model reparsed = parse(mermaid(), serialized);
        QCOMPARE(reparsed.links[0].props.value(QStringLiteral("startHead")).toString(), QStringLiteral("arrow"));
        QCOMPARE(reparsed.links[1]
                     .props.value(QStringLiteral("fragments"))
                     .toArray()[0]
                     .toObject()
                     .value(QStringLiteral("branchLabel"))
                     .toString(),
                 QStringLiteral("Expired token"));
    }

    void mermaidReadsEveryArrow()
    {
        const Model model = parse(mermaid(), QStringLiteral("sequenceDiagram\n"
                                                            "A->B: a\nA-->B: b\nA->>B: c\nA-->>B: d\nA-xB: e\n"
                                                            "A--xB: f\nA-)B: g\nA--)B: h\nA<<-->>B: i\n"
                                                            "A->>A: self; B->>A: two\n"
                                                            "A->>B: wrap: x &amp; y#59; z<br/>w"));
        const QStringList expected{
            QStringLiteral("solid none"),   QStringLiteral("dashed none"), QStringLiteral("solid arrow"),
            QStringLiteral("dashed arrow"), QStringLiteral("solid cross"), QStringLiteral("dashed cross"),
            QStringLiteral("solid open"),   QStringLiteral("dashed open"), QStringLiteral("dashed arrow"),
            QStringLiteral("solid arrow"),  QStringLiteral("solid arrow"), QStringLiteral("solid arrow"),
        };
        QCOMPARE(model.links.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i) {
            const QJsonObject props = model.links[i].props;
            QCOMPARE(props.value(QStringLiteral("line")).toString() + QLatin1Char(' ') +
                         props.value(QStringLiteral("head")).toString(),
                     expected[i]);
            QCOMPARE(props.value(QStringLiteral("order")).toInt(), i);
        }
        QCOMPARE(model.links[8].props.value(QStringLiteral("startHead")).toString(), QStringLiteral("arrow"));
        QCOMPARE(model.links[9].from, model.links[9].to);
        QCOMPARE(model.links[11].props.value(QStringLiteral("label")).toString(), QStringLiteral("x & y; z\nw"));
        // Arrows round-trip one to one.
        const QString serialized = roundTrip(mermaid(), QStringLiteral("sequenceDiagram\nA->B: a\nA-->B: b\nA-xB: e\n"
                                                                       "A--)B: h\nA<<-->>B: i\nA->>B: x#59; y<br/>z"));
        for (const char *line : {"A->B: a", "A-->B: b", "A-xB: e", "A--)B: h", "A<<-->>B: i", "A->>B: x#59; y<br/>z"})
            QVERIFY2(serialized.contains(QLatin1String(line)), qPrintable(serialized));
    }

    void mermaidDeclarationsAndUnknownStatements()
    {
        const Model model = parse(mermaid(), QStringLiteral("sequenceDiagram\n"
                                                            "participant A as Alice<br/>Smith\n"
                                                            "create actor B as Bob\n"
                                                            "actor A\n"
                                                            "title Hello\n"
                                                            "Note over A,B: a note\n"
                                                            "activate A\n"
                                                            "this is nonsense\n"
                                                            "A->>B: hi\n"));
        QCOMPARE(model.nodes.size(), 2);
        QCOMPARE(model.nodes[0].key, QStringLiteral("A"));
        QCOMPARE(model.nodes[0].props.value(QStringLiteral("text")).toString(), QStringLiteral("Alice\nSmith"));
        // A later `actor` declaration turns the participant into an actor.
        QCOMPARE(model.nodes[0].props.value(QStringLiteral("kind")).toString(), QStringLiteral("actor"));
        QCOMPARE(model.nodes[1].props.value(QStringLiteral("text")).toString(), QStringLiteral("Bob"));
        // The title is presentation; the note, the activation and the
        // nonsense are reported.
        QCOMPARE(model.warnings.size(), 3);
        QVERIFY(model.warnings[0].startsWith(QStringLiteral("Not understood: Note over")));
        QVERIFY(model.warnings[2].contains(QStringLiteral("nonsense")));
        // A multi-line alias is written back as <br/>.
        Board board;
        buildBoard(board, mermaid(), model);
        const QString serialized = mermaid().serialize(board, {});
        QVERIFY2(serialized.contains(QStringLiteral("actor A as Alice<br/>Smith")), qPrintable(serialized));
    }

    void plantumlKeepsKindsFragmentsAndDoubleHeads()
    {
        const QString source = QStringLiteral("@startuml\n"
                                              "    actor Client\n"
                                              "    database DB\n"
                                              "    alt available\n"
                                              "      Client <-> DB : read\n"
                                              "    else unavailable\n"
                                              "      DB --> Client : error\n"
                                              "    end\n"
                                              "    @enduml");
        const Model model = parse(plantuml(), source);
        QCOMPARE(model.nodes.size(), 2);
        QCOMPARE(model.nodes[1].key, QStringLiteral("DB"));
        QCOMPARE(model.nodes[1].props.value(QStringLiteral("kind")).toString(), QStringLiteral("database"));
        QCOMPARE(model.links[0].props.value(QStringLiteral("startHead")).toString(), QStringLiteral("arrow"));
        QCOMPARE(model.links[1]
                     .props.value(QStringLiteral("fragments"))
                     .toArray()[0]
                     .toObject()
                     .value(QStringLiteral("branchLabel"))
                     .toString(),
                 QStringLiteral("unavailable"));

        const QString serialized = roundTrip(plantuml(), source);
        QVERIFY2(serialized.contains(QStringLiteral("database DB")), qPrintable(serialized));
        QVERIFY(serialized.contains(QStringLiteral("alt available")));
        QVERIFY(serialized.contains(QStringLiteral("else unavailable")));
        QVERIFY(serialized.contains(QStringLiteral("Client <-> DB : read")));
        QVERIFY(serialized.contains(QStringLiteral("DB --> Client : error")));
        QVERIFY(serialized.startsWith(QStringLiteral("@startuml")));
        QVERIFY(serialized.endsWith(QStringLiteral("end\n@enduml")));
    }

    void plantumlArrowsAliasesAndShortcuts()
    {
        const Model model = parse(plantuml(), QString::fromUtf8(kRichPlantuml));
        QStringList keys;
        for (const Model::Node &node : model.nodes)
            keys.append(node.key);
        QCOMPARE(keys, (QStringList{QStringLiteral("U"), QStringLiteral("W"), QStringLiteral("DB"),
                                    QStringLiteral("Events")}));
        QCOMPARE(model.nodes[0].props.value(QStringLiteral("text")).toString(), QStringLiteral("User"));
        QCOMPARE(model.nodes[1].props.value(QStringLiteral("text")).toString(), QStringLiteral("Web app"));
        QCOMPARE(model.nodes[3].props.value(QStringLiteral("kind")).toString(), QStringLiteral("queue"));
        QCOMPARE(model.links.size(), 7);
        QCOMPARE(model.links[1].props.value(QStringLiteral("activation")).toString(), QStringLiteral("+"));
        QCOMPARE(model.links[2].props.value(QStringLiteral("activation")).toString(), QStringLiteral("-"));
        QCOMPARE(model.links[2].props.value(QStringLiteral("line")).toString(), QStringLiteral("dashed"));
        QCOMPARE(model.links[3].props.value(QStringLiteral("head")).toString(), QStringLiteral("open"));
        QCOMPARE(model.links[4].props.value(QStringLiteral("head")).toString(), QStringLiteral("cross"));
        // `W <- DB` is DB → W.
        QCOMPARE(model.links[5].from, QStringLiteral("DB"));
        QCOMPARE(model.links[5].to, QStringLiteral("W"));
        QCOMPARE(model.links[5].props.value(QStringLiteral("head")).toString(), QStringLiteral("arrow"));
        QCOMPARE(model.links[6].from, model.links[6].to);
        const QJsonObject group = model.links[3].props.value(QStringLiteral("fragments")).toArray()[0].toObject();
        QCOMPARE(group.value(QStringLiteral("kind")).toString(), QStringLiteral("group"));
        QCOMPARE(group.value(QStringLiteral("label")).toString(), QStringLiteral("Cache"));
        QVERIFY(model.warnings.isEmpty());

        // Everything comes back the same from the serialized source.
        const QString serialized = roundTrip(plantuml(), QString::fromUtf8(kRichPlantuml));
        QVERIFY2(serialized.contains(QStringLiteral("W -> DB ++ : query")), qPrintable(serialized));
        QVERIFY(serialized.contains(QStringLiteral("DB --> W -- : rows")));
        QVERIFY(serialized.contains(QStringLiteral("W ->> Events : published")));
        QVERIFY(serialized.contains(QStringLiteral("W ->x DB : lost")));
        QVERIFY(serialized.contains(QStringLiteral("participant \"Web app\" as W")));
        const Model reparsed = parse(plantuml(), serialized);
        QCOMPARE(reparsed.links.size(), model.links.size());
        for (int i = 0; i < model.links.size(); ++i) {
            QCOMPARE(reparsed.links[i].from, model.links[i].from);
            QCOMPARE(reparsed.links[i].to, model.links[i].to);
            for (const char *field : {"label", "line", "head", "startHead", "activation", "fragments"})
                QCOMPARE(reparsed.links[i].props.value(QLatin1String(field)),
                         model.links[i].props.value(QLatin1String(field)));
        }
    }

    void plantumlReportsNotesWithoutBreakingGroups()
    {
        const Model model = parse(plantuml(), QStringLiteral("@startuml\n"
                                                             "loop again\n"
                                                             "A -> B : one\n"
                                                             "note left of A\n"
                                                             "  a long note\n"
                                                             "end note\n"
                                                             "== Divider ==\n"
                                                             "A -> B : two\n"
                                                             "end\n"
                                                             "@enduml"));
        QCOMPARE(model.links.size(), 2);
        // `end note` closes the note, not the loop.
        QCOMPARE(model.links[1].props.value(QStringLiteral("fragments")).toArray().size(), 1);
        QCOMPARE(model.warnings, QStringList{QStringLiteral("Not understood: note left of A")});
    }

    void mermaidRoundTripsARichDiagram()
    {
        const QString serialized = roundTrip(mermaid(), QString::fromUtf8(kRichMermaid));
        const Model original = parse(mermaid(), QString::fromUtf8(kRichMermaid));
        const Model reparsed = parse(mermaid(), serialized);
        QVERIFY(original.warnings.isEmpty());
        QCOMPARE(reparsed.nodes.size(), original.nodes.size());
        for (int i = 0; i < original.nodes.size(); ++i) {
            QCOMPARE(reparsed.nodes[i].key, original.nodes[i].key);
            QCOMPARE(reparsed.nodes[i].props.value(QStringLiteral("text")),
                     original.nodes[i].props.value(QStringLiteral("text")));
            QCOMPARE(reparsed.nodes[i].props.value(QStringLiteral("kind")),
                     original.nodes[i].props.value(QStringLiteral("kind")));
        }
        QCOMPARE(reparsed.links.size(), original.links.size());
        for (int i = 0; i < original.links.size(); ++i) {
            QCOMPARE(reparsed.links[i].from, original.links[i].from);
            QCOMPARE(reparsed.links[i].to, original.links[i].to);
            for (const char *field : {"label", "line", "head", "startHead", "activation", "fragments", "order"})
                QCOMPARE(reparsed.links[i].props.value(QLatin1String(field)),
                         original.links[i].props.value(QLatin1String(field)));
        }
        // `par` branches are written with `and`.
        QVERIFY2(serialized.contains(QStringLiteral("  and Load the news")), qPrintable(serialized));
        // Serializing again changes nothing.
        QCOMPARE(roundTrip(mermaid(), serialized), serialized);
    }

    // The loop of a self-message is what motion and hit-testing see.
    void selfMessageExposesItsLoop()
    {
        Board board;
        board.setFamily(&families::sequence());
        const ParticipantShape *api = participant(board, {{QStringLiteral("text"), QStringLiteral("API")},
                                                          {QStringLiteral("x"), 0},
                                                          {QStringLiteral("y"), 20},
                                                          {QStringLiteral("w"), 150},
                                                          {QStringLiteral("h"), 44}});
        QCOMPARE(api->w, 150.0);
        // One line of text needs 48 px (sidecar's test leaves the shape
        // unmeasured at 44): the lanes start under the measured header.
        QCOMPARE(api->h, 48.0);
        const MessageLink *self = message(board, api->id, api->id, {{QStringLiteral("order"), 0}});
        QVERIFY(self);
        const std::optional<Route> r = self->route(board);
        QVERIFY(r);
        const std::vector<QPointF> expected{{75, 112}, {131, 112}, {131, 130}, {75, 130}};
        QCOMPARE(r->points, expected);
        QVERIFY(self->hitTest(board, QPointF(131, 121), 2));
        QVERIFY(!self->hitTest(board, QPointF(75, 121), 2));
    }

    // Message labels sit right on their link.
    void labelsAreCenteredOnTheirMessage()
    {
        Board board;
        board.setFamily(&families::sequence());
        const ParticipantShape *a = participant(board, {{QStringLiteral("x"), 20}, {QStringLiteral("y"), 20}});
        const ParticipantShape *b = participant(board, {{QStringLiteral("x"), 260}, {QStringLiteral("y"), 20}});
        const MessageLink *call = message(
            board, a->id, b->id, {{QStringLiteral("order"), 0}, {QStringLiteral("label"), QStringLiteral("Call")}});
        const MessageLink *back = message(
            board, a->id, a->id, {{QStringLiteral("order"), 1}, {QStringLiteral("label"), QStringLiteral("Callback")}});
        const Route r = *call->route(board);
        QCOMPARE(call->labelPos(r, board), QPointF((r.a.x() + r.b.x()) / 2, r.a.y()));
        const Route s = *back->route(board);
        QCOMPARE(back->labelPos(s, board), QPointF(s.a.x() + 56, s.a.y() + 9));
        // The loop and its label are part of what is framed.
        QVERIFY(back->bounds(board)->right() > s.a.x() + 56);
        QVERIFY(!call->bounds(board));
    }

    // Filled participants show their next handle on the lifeline, and a
    // second one right under the header.
    void filledParticipantsOfferTheNextHandle()
    {
        Board board;
        board.setFamily(&families::sequence());
        const ParticipantShape *a = participant(
            board,
            {{QStringLiteral("text"), QStringLiteral("Actor")}, {QStringLiteral("x"), 0}, {QStringLiteral("y"), 20}});
        const ParticipantShape *b = participant(board, {{QStringLiteral("text"), QStringLiteral("System")},
                                                        {QStringLiteral("kind"), QStringLiteral("actor")},
                                                        {QStringLiteral("x"), 220},
                                                        {QStringLiteral("y"), 20}});
        for (int order = 0; order < 8; ++order)
            message(board, a->id, b->id,
                    {{QStringLiteral("order"), order},
                     {QStringLiteral("label"), QStringLiteral("Interaction %1").arg(order + 1)}});
        QCOMPARE(board.links().size(), size_t(8));
        const qreal nextY = messageY(board, 8);
        QVERIFY(!a->showConnectionPorts());
        QVERIFY(a->showConnectionPortsOnHover());
        QVERIFY(nextY > a->y + a->h);
        const QList<Port> portsA = a->connectionPorts(board);
        QCOMPARE(portsA.size(), 2);
        QCOMPARE(QPointF(portsA[0].x, portsA[0].y), QPointF(a->cx(), a->y + a->h + 14));
        QCOMPARE(QPointF(portsA[1].x, portsA[1].y), QPointF(a->cx(), nextY));
        const QList<Port> portsB = b->connectionPorts(board);
        QCOMPARE(QPointF(portsB[0].x, portsB[0].y), QPointF(b->cx(), b->y + b->h + 14));
        QCOMPARE(QPointF(portsB[1].x, portsB[1].y), QPointF(b->cx(), nextY));
        QVERIFY(portsA[0].y - (a->y + a->h) <= 44);
        QVERIFY(a->connectionHitTest(board, QPointF(a->cx(), nextY - 20)));
        QVERIFY(b->connectionHitTest(board, QPointF(b->cx(), nextY)));
        QCOMPARE(b->connectionPoint(board, QPointF(a->cx(), nextY), QPointF()), QPointF(b->cx(), nextY));
        QVERIFY(!b->connectionHitTest(board, QPointF(b->cx() + 30, nextY)));
    }

    void newDiagramKeepsOneHandle()
    {
        Board board;
        board.setFamily(&families::sequence());
        const ParticipantShape *a = participant(
            board,
            {{QStringLiteral("kind"), QStringLiteral("actor")}, {QStringLiteral("x"), 0}, {QStringLiteral("y"), 20}});
        const ParticipantShape *b = participant(board, {{QStringLiteral("x"), 220}, {QStringLiteral("y"), 20}});
        const QList<Port> ports = a->connectionPorts(board);
        QCOMPARE(ports.size(), 1);
        QCOMPARE(QPointF(ports[0].x, ports[0].y), QPointF(a->cx(), messageY(board, 0)));
        QVERIFY(ports[0].y - (a->y + a->h) <= 44);
        QCOMPARE(b->connectionPorts(board).size(), 1);
        QCOMPARE(b->connectionPorts(board)[0].y, messageY(board, 0));
    }

    // A message drawn interactively takes the next lane.
    void interactiveMessageTakesTheNextLane()
    {
        Board board;
        board.setFamily(&families::sequence());
        const ParticipantShape *a = participant(board, {{QStringLiteral("x"), 0}});
        const ParticipantShape *b = participant(board, {{QStringLiteral("x"), 220}});
        message(board, a->id, b->id, {{QStringLiteral("order"), 0}});
        const Link *drawn = board.connect(b->id, a->id);
        const Link *again = board.connect(b->id, a->id);
        QVERIFY(drawn && again && drawn != again);
        QCOMPARE(drawn->route(board)->a.y(), messageY(board, 1));
        QCOMPARE(again->route(board)->a.y(), messageY(board, 2));
        QCOMPARE(drawn->toJson().value(QStringLiteral("order")).toInt(), 1);
    }

    void participantGrowsWithItsName()
    {
        Board board;
        board.setFamily(&families::sequence());
        ParticipantShape *p = participant(board, {{QStringLiteral("text"), QStringLiteral("API")}});
        const QSizeF small(p->w, p->h);
        p->text = QStringLiteral("Synchronization service of the distributed documents");
        board.measureShape(*p);
        QVERIFY(p->w > small.width());
        QVERIFY(p->h >= small.height());
        QVERIFY(p->w <= 320);
    }

    // Fragment headers and branches reserve vertical room above their
    // messages.
    void fragmentsReserveRoom()
    {
        Board board;
        board.setFamily(&families::sequence());
        const ParticipantShape *a = participant(
            board,
            {{QStringLiteral("text"), QStringLiteral("A")}, {QStringLiteral("x"), 0}, {QStringLiteral("y"), 20}});
        const ParticipantShape *b = participant(
            board,
            {{QStringLiteral("text"), QStringLiteral("B")}, {QStringLiteral("x"), 220}, {QStringLiteral("y"), 20}});
        const auto fragment = [](const char *keyword, const char *label) {
            return QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("alt-1")},
                                          {QStringLiteral("kind"), QStringLiteral("alt")},
                                          {QStringLiteral("label"), QStringLiteral("valid")},
                                          {QStringLiteral("depth"), 0},
                                          {QStringLiteral("branchKeyword"), QLatin1String(keyword)},
                                          {QStringLiteral("branchLabel"), QLatin1String(label)}}};
        };
        message(board, a->id, b->id, {{QStringLiteral("order"), 0}, {QStringLiteral("fragments"), fragment("", "")}});
        message(board, b->id, a->id,
                {{QStringLiteral("order"), 1}, {QStringLiteral("fragments"), fragment("else", "invalid")}});
        // 24 px for the alt header (sidecar's 132 with an unmeasured 44 px header).
        QCOMPARE(messageY(board, 0), a->y + a->h + 44 + 24);
        QCOMPARE(messageY(board, 1) - messageY(board, 0), 66.0); // the else adds 22 px to the 44 px step
    }

    void fragmentStatementsFollowTheStack()
    {
        const Fragment loop{QStringLiteral("f1"), QStringLiteral("loop"), QStringLiteral("again"), 0, {}, {}};
        const Fragment par{QStringLiteral("f2"), QStringLiteral("par"), QStringLiteral("one"), 1, {}, {}};
        Fragment parBranch = par;
        parBranch.branchLabel = QStringLiteral("two");
        QStringList out;
        QList<Fragment> active = syncFragments(out, {}, {loop, par}, {QStringLiteral("  "), true});
        active = syncFragments(out, active, {loop, parBranch}, {QStringLiteral("  "), true});
        active = syncFragments(out, active, {}, {QStringLiteral("  "), true});
        QCOMPARE(out, (QStringList{QStringLiteral("  loop again"), QStringLiteral("  par one"),
                                   QStringLiteral("  and two"), QStringLiteral("  end"), QStringLiteral("  end")}));
        out.clear();
        syncFragments(out, {loop, par}, {loop, parBranch});
        QCOMPARE(out, QStringList{QStringLiteral("else two")});
    }

    // The family's layout: one row of headers, label widths respected,
    // lanes compacted.
    void arrangeLinesUpTheParticipants()
    {
        Board board;
        board.setFamily(&families::sequence());
        ParticipantShape *a = participant(
            board,
            {{QStringLiteral("text"), QStringLiteral("A")}, {QStringLiteral("x"), 500}, {QStringLiteral("y"), 300}});
        ParticipantShape *b = participant(
            board,
            {{QStringLiteral("text"), QStringLiteral("B")}, {QStringLiteral("x"), 100}, {QStringLiteral("y"), 90}});
        ParticipantShape *c =
            participant(board, {{QStringLiteral("text"), QStringLiteral("C")}, {QStringLiteral("x"), 900}});
        const QString longLabel =
            QStringLiteral("A very long message label that needs a lot of room between lifelines");
        message(board, b->id, a->id, {{QStringLiteral("order"), 4}, {QStringLiteral("label"), longLabel}});
        message(board, a->id, a->id,
                {{QStringLiteral("order"), 9}, {QStringLiteral("label"), QStringLiteral("Think")}});
        message(board, a->id, c->id, {{QStringLiteral("order"), 2}});
        families::sequence().autoArrange(board);
        QCOMPARE(b->x, 0.0);
        for (const ParticipantShape *p : {a, b, c})
            QCOMPARE(p->y, 20.0);
        QVERIFY(b->x + b->w < a->x);
        QVERIFY(a->x + a->w < c->x);
        const qreal labelWidth = text::width(Font(board.style().linkLabel.fontPx), longLabel) + 30;
        QVERIFY(a->cx() - b->cx() >= labelWidth - 0.01);
        QVERIFY(c->x - (a->x + a->w) >= 56 + 20);
        QList<int> orders;
        for (const MessageLink *m : messagesOf(board))
            orders.append(*m->order);
        QCOMPARE(orders, (QList<int>{0, 1, 2}));
    }

    void blankFilesLoad()
    {
        for (const auto &[name, codec] :
             {std::pair{QStringLiteral("a.mmd"), &mermaid()}, std::pair{QStringLiteral("a.puml"), &plantuml()}}) {
            Board board;
            document::Loaded loaded;
            QVERIFY(load(board, name, codec->blank().toUtf8(), &loaded));
            QCOMPARE(loaded.codec, codec);
            QVERIFY(loaded.warnings.isEmpty());
            QCOMPARE(board.shapes().size(), size_t(2));
            QCOMPARE(board.links().size(), size_t(1));
            QCOMPARE(byKey(board, QStringLiteral("A"))->text, QStringLiteral("Actor"));
            QVERIFY(loaded.needsArrange);
            QVERIFY(!loaded.needsRouting);
        }
        QCOMPARE(document::extension(QStringLiteral("sequence")), QStringLiteral("mmd"));
        QVERIFY(document::blank(QStringLiteral("sequence")).startsWith("sequenceDiagram"));
    }

    // save → load keeps the keys, texts, messages and positions; the
    // messages carry no Graphviz curve.
    void saveAndLoadKeepEverything()
    {
        for (const auto &[name, source] : {std::pair{QStringLiteral("rich.mmd"), QByteArray(kRichMermaid)},
                                           std::pair{QStringLiteral("rich.puml"), QByteArray(kRichPlantuml)}}) {
            Board board;
            loadArranged(board, name, source);
            QVERIFY(!board.isEmpty());
            QVERIFY(!graphviz::routeLinks(board));
            for (const auto &l : board.links())
                QVERIFY(!l->spline);
            // A hand-made change of place and color.
            Shape *moved = board.shapes().back().get();
            moved->x += 37;
            moved->customFill = QStringLiteral("#ffcc00");
            const QByteArray saved =
                document::save(board, name.endsWith(QStringLiteral(".mmd")) ? mermaid() : plantuml());
            QVERIFY2(!saved.contains("\"c\":"), saved.constData());

            Board again;
            document::Loaded loaded;
            QVERIFY(load(again, name, saved, &loaded));
            QVERIFY(!loaded.needsArrange);
            QCOMPARE(again.shapes().size(), board.shapes().size());
            QCOMPARE(again.links().size(), board.links().size());
            for (const auto &s : board.shapes()) {
                const ParticipantShape *copy = byKey(again, s->key);
                QVERIFY2(copy, qPrintable(s->key));
                QCOMPARE(copy->text, s->displayText());
                QCOMPARE(copy->kind, dynamic_cast<const ParticipantShape *>(s.get())->kind);
                // The layout comments keep a tenth of a pixel.
                QVERIFY(std::abs(copy->x - s->x) <= 0.05);
                QVERIFY(std::abs(copy->y - s->y) <= 0.05);
                QCOMPARE(copy->customFill, s->customFill);
            }
            const QList<MessageLink *> before = messagesOf(board);
            const QList<MessageLink *> after = messagesOf(again);
            for (int i = 0; i < before.size(); ++i) {
                QCOMPARE(again.shape(after[i]->from)->key, board.shape(before[i]->from)->key);
                QCOMPARE(again.shape(after[i]->to)->key, board.shape(before[i]->to)->key);
                QCOMPARE(after[i]->label, before[i]->label);
                QCOMPARE(after[i]->line, before[i]->line);
                QCOMPARE(after[i]->head, before[i]->head);
                QCOMPARE(after[i]->activation, before[i]->activation);
                QCOMPARE(after[i]->fragments.size(), before[i]->fragments.size());
                for (int k = 0; k < before[i]->fragments.size(); ++k)
                    QCOMPARE(after[i]->fragments[k].toJson(), before[i]->fragments[k].toJson());
            }
            QCOMPARE(document::save(again, *loaded.codec), saved);
        }
    }

    // Undo and redo rebuild messages from their JSON: nothing is lost.
    void historyKeepsMessages()
    {
        Board board;
        loadArranged(board, QStringLiteral("rich.mmd"), kRichMermaid);
        const QByteArray before = board.snapshot();
        MessageLink *first = messagesOf(board).first();
        board.edit([&] {
            first->head = QStringLiteral("cross");
            first->fragments.clear();
        });
        board.undo();
        QCOMPARE(board.snapshot(), before);
        board.redo();
        QCOMPARE(messagesOf(board).first()->head, QStringLiteral("cross"));
    }

    void arrangedDiagramHasNoOverlap()
    {
        Board board;
        loadArranged(board, QStringLiteral("rich.mmd"), kRichMermaid);
        for (const auto &s : board.shapes()) {
            for (const auto &t : board.shapes()) {
                if (s != t)
                    QVERIFY2(!s->rect().intersects(t->rect()), qPrintable(s->key + QLatin1Char('/') + t->key));
            }
        }
        // Every message label fits between its lifelines.
        for (const MessageLink *m : messagesOf(board)) {
            if (m->from == m->to || m->label.isEmpty())
                continue;
            const qreal distance = std::abs(board.shape(m->from)->cx() - board.shape(m->to)->cx());
            QVERIFY(distance >= text::width(Font(board.style().linkLabel.fontPx), m->label) + 30 - 0.01);
        }
        // The lifelines reach below the last message.
        const MessageLink *last = messagesOf(board).back();
        QVERIFY(board.contentBounds().bottom() > messageY(board, *last->order));
    }

    void paintingDoesNotCrash()
    {
        for (const auto &[name, source] : {std::pair{QStringLiteral("rich.mmd"), QByteArray(kRichMermaid)},
                                           std::pair{QStringLiteral("rich.puml"), QByteArray(kRichPlantuml)}}) {
            Board board;
            loadArranged(board, name, source);
            for (const Style &style : {Style::printed(), board.style()}) {
                board.setStyle(style);
                QImage image(1200, 1000, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::white);
                QPainter painter(&image);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.scale(0.6, 0.6);
                Canvas::paintBoard(painter, board);
            }
        }
        // Every head and start head, selected or not.
        Board board;
        board.setFamily(&families::sequence());
        const ParticipantShape *a = participant(board, {{QStringLiteral("x"), 0}});
        const ParticipantShape *b = participant(board, {{QStringLiteral("x"), 220}});
        int order = 0;
        for (const char *head : {"arrow", "open", "cross", "none"}) {
            message(board, a->id, b->id,
                    {{QStringLiteral("order"), order++},
                     {QStringLiteral("head"), QLatin1String(head)},
                     {QStringLiteral("startHead"), QLatin1String(head)},
                     {QStringLiteral("customWidth"), 5}});
            message(board, a->id, a->id,
                    {{QStringLiteral("order"), order++}, {QStringLiteral("head"), QLatin1String(head)}});
        }
        QImage image(600, 600, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        for (const auto &l : board.links())
            l->draw(painter, board, true);
        ShapeState state;
        state.selected = true;
        a->draw(painter, board, state);
    }
};

QTEST_MAIN(TestDiagramSequence)
#include "tst_diagram_sequence.moc"
