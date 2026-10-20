// SPDX-License-Identifier: GPL-3.0-only

// The user journey family (sidecar's journey-diagram.js and
// fence/mermaid-journey.js): the Mermaid journey codec, the task cards and
// their measures, the layout and the drawing.

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Canvas.h"
#include "viewers/diagram/Document.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/Graphviz.h"
#include "viewers/diagram/families/Families.h"
#include "viewers/diagram/families/journey/Journey.h"

#include <QImage>
#include <QPainter>
#include <QtTest>

#include <optional>

using namespace diagram;
using namespace diagram::journey;

namespace {

const char *const kFixture = R"(journey
      title Inscription
      section Découverte
        Ouvre la page: 4: Visiteur
      section Activation
        Confirme son email: 3: Utilisateur)";

const Codec &codec()
{
    const Codec *c = findCodec(QStringLiteral("mermaid-journey"));
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

bool loadInto(Board &board, const QByteArray &content, document::Loaded *loaded = nullptr)
{
    QString error;
    std::optional<document::Loaded> result = document::load(board, QStringLiteral("journey.mmd"), content, &error);
    if (!result) {
        qWarning() << error;
        return false;
    }
    if (loaded)
        *loaded = std::move(*result);
    return true;
}

JourneyTaskShape *step(const Board &board, const QString &key)
{
    for (const auto &s : board.shapes()) {
        if (s->key == key)
            return dynamic_cast<JourneyTaskShape *>(s.get());
    }
    return nullptr;
}

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

class TestDiagramJourney : public QObject {
    Q_OBJECT

private slots:

    void registersTheFamily()
    {
        const Family *family = findFamily(QStringLiteral("journey"));
        QVERIFY(family);
        QCOMPARE(family->title, QStringLiteral("User journey"));
        QCOMPARE(codec().language(), QStringLiteral("mermaid"));
        QCOMPARE(&codec().family(), family);
        QCOMPARE(codecFor(QStringLiteral("mermaid"), QString::fromUtf8(kFixture)), &codec());
        QCOMPARE(defaultCodec(*family), &codec());
        QVERIFY(!codec().matches(QStringLiteral("erDiagram\n")));
    }

    // diagram-families.test.js: the registry, the model, the round trip.
    void parsesAndSerializesTheFixture()
    {
        const Model model = parse(QString::fromUtf8(kFixture));
        QCOMPARE(model.nodes.size(), 2);
        QCOMPARE(model.links.size(), 1);
        QVERIFY(model.warnings.isEmpty());
        QCOMPARE(model.meta.value(QStringLiteral("title")).toString(), QStringLiteral("Inscription"));
        QCOMPARE(model.meta.value(QStringLiteral("rankdir")).toString(), QStringLiteral("LR"));
        const QJsonObject first = model.nodes[0].props;
        QCOMPARE(model.nodes[0].key, QStringLiteral("step1"));
        QCOMPARE(first.value(QStringLiteral("text")).toString(), QStringLiteral("Ouvre la page"));
        QCOMPARE(first.value(QStringLiteral("score")).toInt(), 4);
        QCOMPARE(jsonStrings(first, QStringLiteral("actors")), QStringList{QStringLiteral("Visiteur")});
        QCOMPARE(first.value(QStringLiteral("section")).toString(), QStringLiteral("Découverte"));
        QCOMPARE(model.links[0].from, QStringLiteral("step1"));
        QCOMPARE(model.links[0].to, QStringLiteral("step2"));

        Board board;
        buildBoard(board, codec(), model);
        const QString serialized = codec().serialize(board, model.meta);
        QCOMPARE(serialized, QStringLiteral("journey\n  title Inscription\n  section Découverte\n"
                                            "    Ouvre la page: 4: Visiteur\n  section Activation\n"
                                            "    Confirme son email: 3: Utilisateur"));
        QCOMPARE(parse(serialized).nodes.size(), 2);
    }

    void readsTheRestOfTheGrammar()
    {
        const Model model = parse(QStringLiteral("journey\n  Before any section: 5\n  Two actors: 2: Ana, Bo ,\n"
                                                 "  Out of range: 7: Me\n"));
        QCOMPARE(model.nodes.size(), 2);
        QCOMPARE(model.nodes[0].props.value(QStringLiteral("section")).toString(), QStringLiteral("Journey"));
        QCOMPARE(jsonStrings(model.nodes[0].props, QStringLiteral("actors")), QStringList());
        QCOMPARE(jsonStrings(model.nodes[1].props, QStringLiteral("actors")),
                 (QStringList{QStringLiteral("Ana"), QStringLiteral("Bo")}));
        QCOMPARE(model.warnings, QStringList{QStringLiteral("Not understood: Out of range: 7: Me")});

        // A colon in a task's text would end it: it is written as a dash.
        Board board;
        buildBoard(board, codec(), model);
        dynamic_cast<JourneyTaskShape *>(board.shapes().front().get())->text = QStringLiteral("Step: one");
        const QString serialized = codec().serialize(board, model.meta);
        QVERIFY(serialized.contains(QStringLiteral("    Step — one: 5\n")));
        QVERIFY(!serialized.contains(QStringLiteral("title")));
        QCOMPARE(serialized.count(QStringLiteral("section Journey")), 1);
    }

    // shape-board-layout.test.js: a step keeps all its texts in its box.
    void keepsItsTextsInsideTheBox()
    {
        Board board;
        board.setFamily(&families::journey());
        const auto *shape = dynamic_cast<JourneyTaskShape *>(board.addShape(
            QStringLiteral("journey"),
            QJsonObject{
                {QStringLiteral("text"),
                 QStringLiteral("Compare les différentes offres disponibles avant de poursuivre l'inscription")},
                {QStringLiteral("section"), QStringLiteral("Découverte et comparaison des solutions")},
                {QStringLiteral("actors"), QJsonArray{QStringLiteral("Responsable des achats internationaux"),
                                                      QStringLiteral("Administrateur de la plateforme")}}}));
        QVERIFY(shape);
        QCOMPARE(shape->score, 3);
        const qreal inner = shape->w - 32;
        const JourneyTaskShape::TextLayout layout = shape->textLayout(inner);
        for (const QString &line : layout.header)
            QVERIFY2(text::width(Font(11), line) <= inner, qPrintable(line));
        for (const QString &line : layout.title)
            QVERIFY2(text::width(Font(14, 600), line) <= inner, qPrintable(line));
        for (const QString &line : layout.actors)
            QVERIFY2(text::width(Font(13), line) <= inner, qPrintable(line));
        QVERIFY(layout.header.size() > 1);
        QVERIFY(layout.title.size() > 1);
        QVERIFY(layout.actors.size() > 1);
        QVERIFY(shape->h > 96); // the box grows for its extra lines
        QVERIFY(shape->w <= 240);

        // A short step keeps the minimum card.
        Shape *small =
            board.addShape(QStringLiteral("journey"), QJsonObject{{QStringLiteral("text"), QStringLiteral("Go")}});
        QCOMPARE(small->w, 170.0);
        QCOMPARE(small->h, 96.0);
        QCOMPARE(dynamic_cast<JourneyTaskShape *>(small)->section, QStringLiteral("Journey"));
    }

    void exposesItsProperties()
    {
        Board board;
        board.setFamily(&families::journey());
        auto *shape = dynamic_cast<JourneyTaskShape *>(board.addShape(QStringLiteral("journey"), {}));
        QCOMPARE(shape->text, QStringLiteral("Step"));
        QStringList ids;
        for (const Property &p : shape->properties())
            ids.append(p.id);
        QCOMPARE(ids, (QStringList{QStringLiteral("section"), QStringLiteral("score"), QStringLiteral("actors")}));
        shape->setPropertyValue(QStringLiteral("score"), QStringLiteral("5"));
        shape->setPropertyValue(QStringLiteral("actors"), QStringLiteral("Ana, , Bo"));
        QCOMPARE(shape->propertyValue(QStringLiteral("score")).toString(), QStringLiteral("5"));
        QCOMPARE(shape->actors, (QStringList{QStringLiteral("Ana"), QStringLiteral("Bo")}));
        shape->setPropertyValue(QStringLiteral("score"), QStringLiteral("9"));
        QCOMPARE(shape->score, 5);
        const QJsonObject json = shape->toJson();
        QCOMPARE(json.value(QStringLiteral("score")).toInt(), 5);
        QCOMPARE(jsonStrings(json, QStringLiteral("actors")),
                 (QStringList{QStringLiteral("Ana"), QStringLiteral("Bo")}));
    }

    void savesAndLoadsDocuments()
    {
        Board blank;
        document::Loaded loaded;
        QVERIFY(loadInto(blank, document::blank(QStringLiteral("journey")), &loaded));
        QCOMPARE(loaded.codec, &codec());
        QCOMPARE(blank.shapes().size(), size_t(2));
        QCOMPARE(blank.links().size(), size_t(1));

        const QByteArray source = "journey\n  title Sign up\n  section Discover\n    Read the page: 4: Visitor\n"
                                  "    Compare the offers with a rather long description: 2: Visitor, Buyer\n"
                                  "  section Activate\n    Create an account: 3: User\n    Confirm the email: 1: User\n"
                                  "    First success: 5: User, Admin\n";
        Board board;
        QVERIFY(loadInto(board, source, &loaded));
        QVERIFY(loaded.needsArrange);
        graphviz::ArrangeOptions options;
        options.rankdir = board.meta.value(QStringLiteral("rankdir")).toString();
        QVERIFY(graphviz::autoArrange(board, options));
        QVERIFY(!overlaps(board));
        // Left to right, in order.
        for (int i = 1; i < 5; ++i)
            QVERIFY(step(board, QStringLiteral("step%1").arg(i + 1))->x >
                    step(board, QStringLiteral("step%1").arg(i))->x);

        const QByteArray saved = document::save(board, codec());
        QVERIFY(saved.startsWith(source.trimmed()));
        Board again;
        QVERIFY(loadInto(again, saved, &loaded));
        QVERIFY(!loaded.needsArrange);
        QCOMPARE(again.shapes().size(), size_t(5));
        QCOMPARE(again.links().size(), size_t(4));
        QCOMPARE(again.meta.value(QStringLiteral("title")).toString(), QStringLiteral("Sign up"));
        for (const auto &s : board.shapes()) {
            const auto *original = dynamic_cast<const JourneyTaskShape *>(s.get());
            const JourneyTaskShape *read = step(again, s->key);
            QVERIFY(read);
            QCOMPARE(read->text, original->text);
            QCOMPARE(read->score, original->score);
            QCOMPARE(read->actors, original->actors);
            QCOMPARE(read->section, original->section);
            QVERIFY(std::abs(read->x - s->x) < 0.1);
            QVERIFY(std::abs(read->y - s->y) < 0.1);
        }
        QCOMPARE(document::source(again, codec()), document::source(board, codec()));

        // Undo and redo rebuild the cards from their JSON.
        const QString before = document::source(again, codec());
        again.edit([&] { step(again, QStringLiteral("step2"))->score = 4; });
        QVERIFY(document::source(again, codec()).contains(QStringLiteral("long description: 4: Visitor, Buyer")));
        again.undo();
        QCOMPARE(document::source(again, codec()), before);
    }

    void drawsWithoutCrashing()
    {
        Board board;
        QVERIFY(loadInto(board, document::blank(QStringLiteral("journey"))));
        QVERIFY(graphviz::autoArrange(board, {}));
        board.shapes().front()->customFill = QStringLiteral("#ffcdd2");
        board.selectAll();
        QImage image(1200, 800, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        Canvas::paintBoard(painter, board);
        Style plain = board.style();
        plain.tones.colorful = false;
        board.setStyle(plain);
        Canvas::paintBoard(painter, board);
        painter.end();
        QVERIFY(!image.isNull());
    }
};

QTEST_MAIN(TestDiagramJourney)
#include "tst_diagram_journey.moc"
