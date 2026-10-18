// The mind map and product vision families, ported from sidecar
// (mindmap-diagram.js, product-vision-diagram.js and their fences), with the
// tests of sidecar's product-vision.test.js and the mind map parts of its
// family tests that make sense outside of a browser.

#include "viewers/diagram/Board.h"
#include "viewers/diagram/Canvas.h"
#include "viewers/diagram/Document.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/Graphviz.h"
#include "viewers/diagram/families/Families.h"
#include "viewers/diagram/families/mindmap/Mindmap.h"
#include "viewers/diagram/families/productvision/ProductVision.h"

#include <QImage>
#include <QJsonArray>
#include <QPainter>
#include <QRegularExpression>
#include <QtTest>

using namespace diagram;

namespace {

const Codec &mindmapCodec()
{
    return *findCodec(QStringLiteral("mermaid-mindmap"));
}

const Codec &visionCodec()
{
    return *findCodec(QStringLiteral("mermaid-product-vision"));
}

Model parsed(const Codec &codec, const QString &source)
{
    Model model;
    QString error;
    if (!codec.parse(source, model, &error))
        qWarning() << error;
    return model;
}

const Model::Node *node(const Model &model, const QString &key)
{
    for (const Model::Node &n : model.nodes) {
        if (n.key == key)
            return &n;
    }
    return nullptr;
}

QStringList texts(const Model &model)
{
    QStringList result;
    for (const Model::Node &n : model.nodes)
        result.append(n.props.value(QStringLiteral("text")).toString());
    return result;
}

QList<int> depths(const Model &model)
{
    QList<int> result;
    for (const Model::Node &n : model.nodes)
        result.append(n.props.value(QStringLiteral("depth")).toInt());
    return result;
}

// Links as "parent text > child text".
QStringList branches(const Model &model)
{
    QHash<QString, QString> textOf;
    for (const Model::Node &n : model.nodes)
        textOf.insert(n.key, n.props.value(QStringLiteral("text")).toString());
    QStringList result;
    for (const Model::Edge &e : model.links)
        result.append(textOf.value(e.from) + QStringLiteral(" > ") + textOf.value(e.to));
    return result;
}

QStringList roles(const Model &model)
{
    QStringList result;
    for (const Model::Node &n : model.nodes)
        result.append(n.props.value(QStringLiteral("role")).toString());
    return result;
}

QStringList arrows(const Model &model)
{
    QStringList result;
    for (const Model::Edge &e : model.links)
        result.append(e.from + QLatin1Char('>') + e.to);
    return result;
}

Shape *byKey(const Board &board, const QString &key)
{
    for (const auto &s : board.shapes()) {
        if (s->key == key)
            return s.get();
    }
    return nullptr;
}

bool loadInto(Board &board, const QString &name, const QByteArray &content, document::Loaded *loaded = nullptr)
{
    document::Loaded result;
    QString error;
    const bool ok = document::load(board, name, content, result, &error);
    if (!ok)
        qWarning() << error;
    if (loaded)
        *loaded = result;
    return ok;
}

bool overlaps(const Board &board)
{
    const auto &shapes = board.shapes();
    for (size_t i = 0; i < shapes.size(); ++i) {
        for (size_t j = i + 1; j < shapes.size(); ++j) {
            if (shapes[i]->rect().adjusted(1, 1, -1, -1).intersects(shapes[j]->rect().adjusted(1, 1, -1, -1))) {
                qWarning() << shapes[i]->key << shapes[i]->rect() << shapes[j]->key << shapes[j]->rect();
                return true;
            }
        }
    }
    return false;
}

void paints(const Board &board)
{
    QImage image(800, 600, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    Canvas::paintBoard(painter, board);
}

const char *const kProduct = "mindmap\n"
                             "  root((Produit))\n"
                             "    Documents\n"
                             "      Diagrammes\n"
                             "    Agent";

} // namespace

class TestDiagramMindmap : public QObject {
    Q_OBJECT

private slots:
    // --- Mind map: the codec

    void readsTheBlankMindMap()
    {
        const Codec &codec = mindmapCodec();
        QCOMPARE(codec.language(), QStringLiteral("mermaid"));
        QCOMPARE(&codec.family(), &families::mindmap());
        QVERIFY(codec.arrangeWithGraphviz());
        QCOMPARE(codecFor(QStringLiteral("mermaid"), codec.blank()), &codec);
        const Model model = parsed(codec, codec.blank());
        QCOMPARE(texts(model), (QStringList{QStringLiteral("Central idea"), QStringLiteral("First branch"),
                                            QStringLiteral("First path"), QStringLiteral("Second branch")}));
        QCOMPARE(depths(model), (QList<int>{0, 1, 2, 1}));
        QCOMPARE(branches(model), (QStringList{QStringLiteral("Central idea > First branch"),
                                               QStringLiteral("First branch > First path"),
                                               QStringLiteral("Central idea > Second branch")}));
        QCOMPARE(model.nodes.first().key, QStringLiteral("root"));
        // twopi: a radial tree around the root.
        QCOMPARE(model.meta.value(QStringLiteral("engine")).toString(), QStringLiteral("twopi"));
        QVERIFY(model.warnings.isEmpty());
    }

    void readsNodeShapesAndDecorations()
    {
        const Model model = parsed(mindmapCodec(), QStringLiteral("mindmap\n"
                                                                  "  a((Circle))\n"
                                                                  "    b))Bang((\n"
                                                                  "    c{{Hexagon}}\n"
                                                                  "    d)Cloud(\n"
                                                                  "    e([Stadium])\n"
                                                                  "    f[Square]\n"
                                                                  "      g(Rounded)\n"
                                                                  "    h[\"Quoted (with) brackets\"]\n"
                                                                  "    Bare text ::icon(fa fa-book)\n"
                                                                  "    i[Classy]:::urgent\n"
                                                                  "    j[Two<br>lines]\n"
                                                                  "  %% a comment\n"));
        QCOMPARE(texts(model),
                 (QStringList{QStringLiteral("Circle"), QStringLiteral("Bang"), QStringLiteral("Hexagon"),
                              QStringLiteral("Cloud"), QStringLiteral("Stadium"), QStringLiteral("Square"),
                              QStringLiteral("Rounded"), QStringLiteral("Quoted (with) brackets"),
                              QStringLiteral("Bare text"), QStringLiteral("Classy"), QStringLiteral("Two\nlines")}));
        QCOMPARE(model.nodes.at(0).key, QStringLiteral("a"));
        QCOMPARE(model.nodes.at(5).key, QStringLiteral("f"));
        QCOMPARE(depths(model), (QList<int>{0, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1}));
        QVERIFY(branches(model).contains(QStringLiteral("Square > Rounded")));
    }

    void followsTheIndentation()
    {
        // Deeper lines are children; a line back at a sibling's indent closes
        // the branches in between, whatever the indent step.
        const Model model =
            parsed(mindmapCodec(), QStringLiteral("mindmap\n root\n   A\n      A1\n        A11\n   B\n      B1\n"));
        QCOMPARE(depths(model), (QList<int>{0, 1, 2, 3, 1, 2}));
        QCOMPARE(branches(model),
                 (QStringList{QStringLiteral("root > A"), QStringLiteral("A > A1"), QStringLiteral("A1 > A11"),
                              QStringLiteral("root > B"), QStringLiteral("B > B1")}));
    }

    void keepsKeysUnique()
    {
        // Bare texts are keyed by their slug, never taking a source id.
        const Model model = parsed(
            mindmapCodec(), QStringLiteral("mindmap\n  Idea\n    Idea\n    Idea[Other]\n    x[Same]\n    x[Again]\n"));
        QSet<QString> keys;
        for (const Model::Node &n : model.nodes)
            keys.insert(n.key);
        QCOMPARE(keys.size(), model.nodes.size());
        QCOMPARE(node(model, QStringLiteral("Idea"))->props.value(QStringLiteral("text")).toString(),
                 QStringLiteral("Other"));
        QCOMPARE(node(model, QStringLiteral("x"))->props.value(QStringLiteral("text")).toString(),
                 QStringLiteral("Same"));
    }

    void matchesOnlyMindMaps()
    {
        const Codec &codec = mindmapCodec();
        QVERIFY(codec.matches(QStringLiteral("mindmap\n  root((A))")));
        QVERIFY(codec.matches(QStringLiteral("\n  mindmap\n  root((A))")));
        QVERIFY(!codec.matches(QStringLiteral("flowchart TB\n  A --> B")));
        QVERIFY(!codec.matches(QStringLiteral("mindmaps\n  A")));
    }

    // sidecar: "aller-retour stable" for the mind map.
    void writesTheSourceBackStably()
    {
        const Codec &codec = mindmapCodec();
        for (const QString &source : {QString::fromUtf8(kProduct), codec.blank()}) {
            Board board;
            const Model model = parsed(codec, source);
            buildBoard(board, codec, model);
            const QString written = codec.serialize(board, board.meta);
            const Model again = parsed(codec, written);
            QCOMPARE(texts(again), texts(model));
            QCOMPARE(depths(again), depths(model));
            QCOMPARE(branches(again), branches(model));
            QStringList keys, keysAgain;
            for (const Model::Node &n : model.nodes)
                keys.append(n.key);
            for (const Model::Node &n : again.nodes)
                keysAgain.append(n.key);
            QCOMPARE(keysAgain, keys);
        }
        // The usual form is kept: bare texts, the root as a circle.
        Board board;
        buildBoard(board, codec, parsed(codec, codec.blank()));
        QCOMPARE(codec.serialize(board, board.meta), codec.blank());
    }

    void writesWhatTheTreeCannotSayAsBranches()
    {
        const Codec &codec = mindmapCodec();
        Board board;
        board.setFamily(&families::mindmap());
        Shape *root =
            board.addShape({}, {{QStringLiteral("text"), QStringLiteral("Root")}, {QStringLiteral("depth"), 0}});
        Shape *child = board.addShape({}, {{QStringLiteral("text"), QStringLiteral("Child (draft)")}});
        Shape *other = board.addShape({}, {{QStringLiteral("text"), QStringLiteral("Another root")}});
        Shape *empty = board.addShape({}, {{QStringLiteral("text"), QString()}});
        Shape *a = board.addShape({}, {{QStringLiteral("text"), QStringLiteral("Loop A")}});
        Shape *b = board.addShape({}, {{QStringLiteral("text"), QStringLiteral("Loop B")}});
        board.addLink({}, root->id, child->id);
        board.addLink({}, other->id, empty->id);
        board.addLink({}, a->id, b->id);
        board.addLink({}, b->id, a->id);
        const QString written = codec.serialize(board, board.meta);
        const Model model = parsed(codec, written);
        // One root; the other root and the cycle become top-level branches;
        // an empty text is an idea; brackets are quoted.
        QCOMPARE(texts(model),
                 (QStringList{QStringLiteral("Root"), QStringLiteral("Child (draft)"), QStringLiteral("Another root"),
                              QStringLiteral("Idea"), QStringLiteral("Loop A"), QStringLiteral("Loop B")}));
        QCOMPARE(depths(model), (QList<int>{0, 1, 1, 2, 1, 2}));
        QVERIFY(written.contains(QStringLiteral("[\"Child (draft)\"]")));
    }

    // --- Mind map: the shapes

    void measuresByDepth()
    {
        Board board;
        board.setFamily(&families::mindmap());
        const Font fonts[] = {Font(16, 600), Font(14, 500), Font(13)};
        const qreal padX[] = {24, 18, 10}, padY[] = {16, 12, 6}, lineHeights[] = {23, 20, 19};
        for (int depth : {0, 1, 2, 5}) {
            Shape *s = board.addShape(
                {}, {{QStringLiteral("text"), QStringLiteral("Idea")}, {QStringLiteral("depth"), depth}});
            const int l = mindmap::level(depth);
            QCOMPARE(s->w, text::width(fonts[l], QStringLiteral("Idea")) + padX[l] * 2);
            QCOMPARE(s->h, lineHeights[l] + padY[l] * 2);
        }
        // A long text wraps at the level's width.
        Shape *wide = board.addShape({}, {{QStringLiteral("text"), QStringLiteral("word ").repeated(40).trimmed()},
                                          {QStringLiteral("depth"), 1}});
        QVERIFY(wide->w <= 190 + 18 * 2 + 0.5);
        QVERIFY(wide->h > 20 + 12 * 2);
    }

    void nodesAreResizable()
    {
        Board board;
        board.setFamily(&families::mindmap());
        Shape *s = board.addShape({}, {{QStringLiteral("text"), QStringLiteral("Idée")}, {QStringLiteral("depth"), 1}});
        const QSizeF minimum(s->resizeMinWidth, s->resizeMinHeight);
        s->resizeTo(minimum.width() + 90, minimum.height() + 60, board);
        QVERIFY(s->w >= minimum.width() + 90);
        QVERIFY(s->h >= minimum.height() + 60);
    }

    void branchesThinOutFromTheRoot()
    {
        Board board;
        const Codec &codec = mindmapCodec();
        buildBoard(board, codec, parsed(codec, codec.blank()));
        for (const auto &l : board.links()) {
            const auto *from = dynamic_cast<const mindmap::MindNode *>(board.shape(l->from));
            QVERIFY(from);
            const QString width = from->depth == 0 ? QStringLiteral("2.500") : QStringLiteral("1.750");
            QCOMPARE(l->graphvizAttributes(board),
                     (QStringList{QStringLiteral("arrowhead=none"), QStringLiteral("penwidth=") + width}));
            QCOMPARE(l->drawStyle(board, false).width, from->depth == 0 ? 2.5 : 1.75);
            QCOMPARE(l->drawStyle(board, false).color, board.style().palette.lifeline);
        }
    }

    void depthFollowsTheBranches()
    {
        Board board;
        const Codec &codec = mindmapCodec();
        buildBoard(board, codec, parsed(codec, codec.blank()));
        Shape *path = byKey(board, QStringLiteral("First_path"));
        QVERIFY(path);
        // A new idea is a bubble; attached under a leaf, it is a leaf too.
        Shape *created =
            board.createShapeAt(QPointF(500, 500), {}, {{QStringLiteral("text"), QStringLiteral("New")}}, false);
        QCOMPARE(static_cast<mindmap::MindNode *>(created)->depth, 1);
        const qreal bubbleHeight = created->h;
        QVERIFY(board.connect(path->id, created->id));
        board.resolveLabels();
        QCOMPARE(static_cast<mindmap::MindNode *>(created)->depth, 3);
        QVERIFY(created->h < bubbleHeight);
    }

    // --- Mind map: files, layout, drawing

    void savesAndLoadsMindMaps()
    {
        Board board;
        document::Loaded loaded;
        QVERIFY(loadInto(board, QStringLiteral("ideas.mmd"), QByteArray(kProduct), &loaded));
        QCOMPARE(board.family(), &families::mindmap());
        QVERIFY(loaded.needsArrange);
        graphviz::ArrangeOptions options;
        options.engine = board.meta.value(QStringLiteral("engine")).toString();
        QString error;
        QVERIFY2(graphviz::autoArrange(board, options, &error), qPrintable(error));
        QVERIFY(!overlaps(board));
        Shape *agent = byKey(board, QStringLiteral("Agent"));
        QVERIFY(agent);
        agent->x += 37;
        const QByteArray saved = document::save(board, *loaded.codec);
        QVERIFY(saved.startsWith("mindmap\n  root((Produit))\n    Documents\n      Diagrammes\n    Agent\n"));

        Board again;
        document::Loaded reloaded;
        QVERIFY(loadInto(again, QStringLiteral("ideas.mmd"), saved, &reloaded));
        QVERIFY(!reloaded.needsArrange);
        QVERIFY(reloaded.unplaced.isEmpty());
        QCOMPARE(again.shapes().size(), board.shapes().size());
        QCOMPARE(again.links().size(), board.links().size());
        for (const auto &s : board.shapes()) {
            const Shape *t = byKey(again, s->key);
            QVERIFY(t);
            QCOMPARE(t->displayText(), s->displayText());
            QCOMPARE(t->x, std::round(s->x * 10) / 10);
            QCOMPARE(t->y, std::round(s->y * 10) / 10);
            QCOMPARE(static_cast<const mindmap::MindNode *>(t)->depth,
                     static_cast<const mindmap::MindNode *>(s.get())->depth);
        }
        paints(again);
    }

    void keepsPlacesWhenABranchIsInserted()
    {
        // Claude inserts a branch before the others: they keep their places.
        Board board;
        document::Loaded loaded;
        QVERIFY(loadInto(board, QStringLiteral("ideas.mmd"), QByteArray(kProduct), &loaded));
        graphviz::ArrangeOptions options;
        options.engine = QStringLiteral("twopi");
        QVERIFY(graphviz::autoArrange(board, options));
        const QPointF agent(byKey(board, QStringLiteral("Agent"))->x, byKey(board, QStringLiteral("Agent"))->y);
        QString saved = QString::fromUtf8(document::save(board, *loaded.codec));
        saved.replace(QStringLiteral("    Documents\n"), QStringLiteral("    Inserted\n    Documents\n"));
        Board again;
        document::Loaded reloaded;
        QVERIFY(loadInto(again, QStringLiteral("ideas.mmd"), saved.toUtf8(), &reloaded));
        QCOMPARE(reloaded.unplaced.size(), 1);
        const Shape *moved = byKey(again, QStringLiteral("Agent"));
        QCOMPARE(QPointF(moved->x, moved->y),
                 QPointF(std::round(agent.x() * 10) / 10, std::round(agent.y() * 10) / 10));
    }

    void drawsEveryLevel()
    {
        Board board;
        const Codec &codec = mindmapCodec();
        buildBoard(board, codec, parsed(codec, codec.blank()));
        board.selectAll();
        paints(board);
        board.paintSelection(QStringLiteral("#ffcc80"));
        paints(board);
    }

    // --- Product vision: the codec (sidecar's product-vision.test.js)

    void readsTheBlankProductVision()
    {
        const Codec &codec = visionCodec();
        const QString blank = codec.blank();
        QCOMPARE(codec.language(), QStringLiteral("mermaid"));
        QVERIFY(blank.startsWith(QStringLiteral("%% product-vision\nmindmap\n")));
        QVERIFY(blank.contains(QStringLiteral("\n    emotions[")));
        // Found before the plain mind map, which the source also is.
        QCOMPARE(codecFor(QStringLiteral("mermaid"), blank), &codec);
        QVERIFY(mindmapCodec().matches(blank));
        QVERIFY(codec.arrangeWithGraphviz());
        const Family &family = codec.family();
        QCOMPARE(family.id, QStringLiteral("productvision"));
        QCOMPARE(family.graphvizSplines, QStringLiteral("line"));
        QVERIFY(family.alwaysArrange);
        QVERIFY(!family.allowDeletion);
        QVERIFY(&family != &families::mindmap());

        const Model model = parsed(codec, blank);
        QCOMPARE(model.meta.value(QStringLiteral("engine")).toString(), QStringLiteral("dot"));
        QCOMPARE(model.meta.value(QStringLiteral("rankdir")).toString(), QStringLiteral("TB"));
        QCOMPARE(model.meta.value(QStringLiteral("purpose")).toString(), QStringLiteral("product-vision"));
        QCOMPARE(roles(model),
                 (QStringList{QStringLiteral("product"), QStringLiteral("emotions"), QStringLiteral("problem"),
                              QStringLiteral("users"), QStringLiteral("value"), QStringLiteral("capabilities"),
                              QStringLiteral("journeys"), QStringLiteral("unknowns")}));
        QCOMPARE(arrows(model), (QStringList{QStringLiteral("product>emotions"), QStringLiteral("emotions>problem"),
                                             QStringLiteral("emotions>users"), QStringLiteral("emotions>value"),
                                             QStringLiteral("problem>capabilities"), QStringLiteral("users>journeys"),
                                             QStringLiteral("value>unknowns")}));
        const Model::Node *product = node(model, QStringLiteral("product"));
        QCOMPARE(product->props.value(QStringLiteral("title")).toString(), QStringLiteral("My product"));
        QCOMPARE(product->props.value(QStringLiteral("items")).toArray().first().toString(),
                 QStringLiteral("The human change this product aims to make possible"));
        QCOMPARE(node(model, QStringLiteral("value"))->props.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Value proposition"));

        Board board;
        buildBoard(board, codec, model);
        const QString source = codec.serialize(board, board.meta);
        QVERIFY(source.startsWith(QStringLiteral("%% product-vision\nmindmap")));
        QCOMPARE(source, blank);
        QVERIFY(parsed(mindmapCodec(), source).nodes.size() > model.nodes.size());
    }

    void readsSidecarsMarker()
    {
        const Codec &codec = visionCodec();
        QString source = codec.blank();
        source.replace(QStringLiteral("%% product-vision"), QStringLiteral("%% sciilo:product-vision"));
        QCOMPARE(codecFor(QStringLiteral("mermaid"), source), &codec);
        QCOMPARE(parsed(codec, source).nodes.size(), 8);
        // Without a marker, a plain mind map.
        source.remove(QStringLiteral("%% sciilo:product-vision\n"));
        QCOMPARE(codecFor(QStringLiteral("mermaid"), source), &mindmapCodec());
        QVERIFY(!codec.matches(QStringLiteral("%% product-vision\nflowchart TB\n  A --> B")));
    }

    void hidesEmptyUnknownsButKeepsTheirBranch()
    {
        const Codec &codec = visionCodec();
        QString source = codec.blank();
        static const QRegularExpression unknowns(
            QStringLiteral("\\n    unknowns\\[[^\\n]+\\]\\n      unknowns_1\\[[^\\n]+\\]"));
        source.replace(unknowns, QStringLiteral("\n    unknowns[\"Unknowns\"]"));
        const Model model = parsed(codec, source);
        QVERIFY(!roles(model).contains(QStringLiteral("unknowns")));
        for (const Model::Edge &e : model.links)
            QVERIFY(e.to != QLatin1String("unknowns"));
        Board board;
        buildBoard(board, codec, model);
        // The stable branch stays available for a later patch.
        QVERIFY(codec.serialize(board, board.meta).contains(QStringLiteral("\n    unknowns[")));
    }

    void gathersTheItemsOfEachBranch()
    {
        const Codec &codec = visionCodec();
        const Model model = parsed(codec, QStringLiteral("%% product-vision\n"
                                                         "mindmap\n"
                                                         "  Atlas\n"
                                                         "    vision[Vision]\n"
                                                         "      v1[Maps for everyone]\n"
                                                         "    problem[Problem]\n"
                                                         "      p1[\"Maps are #quot;stale#quot;\"]\n"
                                                         "        p11[Nobody updates them]\n"
                                                         "      p2[Too expensive]\n"
                                                         "    users[Users]\n"));
        // No `product` key: the root is the node nobody points to.
        const Model::Node *product = node(model, QStringLiteral("product"));
        QCOMPARE(product->props.value(QStringLiteral("title")).toString(), QStringLiteral("Atlas"));
        QCOMPARE(product->props.value(QStringLiteral("items")).toArray().size(), 1);
        const QJsonArray problems =
            node(model, QStringLiteral("problem"))->props.value(QStringLiteral("items")).toArray();
        QCOMPARE(problems.size(), 3);
        QCOMPARE(problems.at(1).toString(), QStringLiteral("Nobody updates them"));
        QVERIFY(node(model, QStringLiteral("users"))->props.value(QStringLiteral("items")).toArray().isEmpty());
        // Sections missing from the source are cards all the same, titled.
        QCOMPARE(node(model, QStringLiteral("journeys"))->props.value(QStringLiteral("title")).toString(),
                 QStringLiteral("Key journeys"));

        Board board;
        buildBoard(board, codec, model);
        const QString written = codec.serialize(board, board.meta);
        QVERIFY(written.contains(QStringLiteral("  product((\"Atlas\"))")));
        QVERIFY(written.contains(QStringLiteral("      problem_1[\"Maps are #quot;stale#quot;\"]")));
        QVERIFY(written.contains(QStringLiteral("    users[\"Users\"]\n    value[")));
        const Model again = parsed(codec, written);
        QCOMPARE(node(again, QStringLiteral("problem"))->props.value(QStringLiteral("items")).toArray(), problems);
    }

    // --- Product vision: the cards

    void cardsCannotBeDeletedOrAdded()
    {
        Board board;
        const Codec &codec = visionCodec();
        buildBoard(board, codec, parsed(codec, codec.blank()));
        const size_t shapes = board.shapes().size(), links = board.links().size();
        Shape *card = byKey(board, QStringLiteral("problem"));
        QVERIFY(!board.removeShape(card->id));
        QVERIFY(!board.removeLink(board.links().front()->id));
        board.selectAll();
        QVERIFY(!board.cutSelection());
        board.deleteSelection();
        QCOMPARE(board.shapes().size(), shapes);
        QCOMPARE(board.links().size(), links);
        // The skeleton is the source's: no card or arrow is made by hand.
        QVERIFY(!board.createShapeAt(QPointF(0, 0)));
        QVERIFY(!board.connect(byKey(board, QStringLiteral("users"))->id, byKey(board, QStringLiteral("value"))->id));
        QCOMPARE(board.shapes().size(), shapes);
        QCOMPARE(board.links().size(), links);
    }

    void measuresTheCards()
    {
        Board board;
        const Codec &codec = visionCodec();
        buildBoard(board, codec, parsed(codec, codec.blank()));
        QCOMPARE(byKey(board, QStringLiteral("product"))->w, 430.0);
        QCOMPARE(byKey(board, QStringLiteral("emotions"))->w, 390.0);
        QCOMPARE(byKey(board, QStringLiteral("problem"))->w, 280.0);
        QVERIFY(byKey(board, QStringLiteral("product"))->h >= 138);
        QVERIFY(byKey(board, QStringLiteral("emotions"))->h >= 122);
        QVERIFY(byKey(board, QStringLiteral("problem"))->h >= 120);
        // Items grow a card.
        Shape *users = byKey(board, QStringLiteral("users"));
        const qreal before = users->h;
        auto *card = static_cast<productvision::ProductVisionShape *>(users);
        card->items = QStringList(6, QStringLiteral("A user with a rather long description of their needs"));
        board.measureShape(*card);
        QVERIFY(card->h > before);
        QCOMPARE(card->displayText(), QStringLiteral("Users"));

        // sidecar: resizable, never shorter than its text.
        Board other;
        other.setFamily(&families::productVision());
        Shape *problem = other.addShape(QStringLiteral("productVision"),
                                        {{QStringLiteral("role"), QStringLiteral("problem")},
                                         {QStringLiteral("items"), QJsonArray{QStringLiteral("Besoin")}}});
        const QSizeF minimum(problem->resizeMinWidth, problem->resizeMinHeight);
        problem->resizeTo(minimum.width() + 90, minimum.height() + 60, other);
        QVERIFY(problem->w >= minimum.width() + 90);
        QVERIFY(problem->h >= minimum.height() + 60);
        problem->resizeTo(120, 20, other);
        QVERIFY(problem->h >= minimum.height());
    }

    void editsTheCardsText()
    {
        Board board;
        const Codec &codec = visionCodec();
        buildBoard(board, codec, parsed(codec, codec.blank()));
        TextEditSpec captured;
        board.setTextEditor([&captured](Shape *, Link *, const TextEditSpec &spec) { captured = spec; });
        auto *product = static_cast<productvision::ProductVisionShape *>(byKey(board, QStringLiteral("product")));
        product->onDoubleClick(board);
        QCOMPARE(captured.get(), QStringLiteral("My product\nThe human change this product aims to make possible"));
        QVERIFY(!captured.enterCommits);
        captured.set(QStringLiteral("  Atlas \n\nMaps for all\n"));
        QCOMPARE(product->title, QStringLiteral("Atlas"));
        QCOMPARE(product->items, QStringList{QStringLiteral("Maps for all")});
        captured.set(QString());
        QCOMPARE(product->title, QStringLiteral("Product"));
        auto *users = static_cast<productvision::ProductVisionShape *>(byKey(board, QStringLiteral("users")));
        users->onDoubleClick(board);
        captured.set(QStringLiteral("Designers\nWriters"));
        QCOMPARE(users->items, (QStringList{QStringLiteral("Designers"), QStringLiteral("Writers")}));
        QCOMPARE(users->title, QStringLiteral("Users"));
    }

    void stylesTheCentralArrow()
    {
        Board board;
        const Codec &codec = visionCodec();
        buildBoard(board, codec, parsed(codec, codec.blank()));
        for (const auto &l : board.links()) {
            const bool central = l->from == byKey(board, QStringLiteral("product"))->id;
            const LinkStyle style = l->drawStyle(board, false);
            QCOMPARE(style.width, board.style().link.width + (central ? .9 : 0));
            QCOMPARE(style.color, central ? board.style().shape.selectedStroke : board.style().link.color);
        }
    }

    // --- Product vision: files, layout, drawing

    void savesAndLoadsProductVisions()
    {
        const Codec &codec = visionCodec();
        Board board;
        document::Loaded loaded;
        QVERIFY(loadInto(board, QStringLiteral("vision.mmd"), codec.blank().toUtf8(), &loaded));
        QCOMPARE(loaded.codec, &codec);
        QCOMPARE(board.family(), &families::productVision());
        QVERIFY(loaded.needsArrange);
        graphviz::ArrangeOptions options;
        options.engine = board.meta.value(QStringLiteral("engine")).toString();
        options.rankdir = board.meta.value(QStringLiteral("rankdir")).toString();
        options.splines = board.family()->graphvizSplines;
        QString error;
        QVERIFY2(graphviz::autoArrange(board, options, &error), qPrintable(error));
        QVERIFY(!overlaps(board));
        // Layered from the product down.
        QVERIFY(byKey(board, QStringLiteral("product"))->y < byKey(board, QStringLiteral("emotions"))->y);
        QVERIFY(byKey(board, QStringLiteral("emotions"))->y < byKey(board, QStringLiteral("problem"))->y);
        QVERIFY(byKey(board, QStringLiteral("problem"))->y < byKey(board, QStringLiteral("capabilities"))->y);
        auto *problem = static_cast<productvision::ProductVisionShape *>(byKey(board, QStringLiteral("problem")));
        problem->items.append(QStringLiteral("Second need"));
        const QByteArray saved = document::save(board, codec);
        QVERIFY(saved.contains("      problem_2[\"Second need\"]"));

        Board again;
        document::Loaded reloaded;
        QVERIFY(loadInto(again, QStringLiteral("vision.mmd"), saved, &reloaded));
        QCOMPARE(reloaded.codec, &codec);
        // Always laid out again: its layout belongs to the source.
        QVERIFY(reloaded.needsArrange);
        QCOMPARE(again.shapes().size(), board.shapes().size());
        QCOMPARE(again.links().size(), board.links().size());
        for (const auto &s : board.shapes()) {
            const auto *t = dynamic_cast<const productvision::ProductVisionShape *>(byKey(again, s->key));
            const auto *u = static_cast<const productvision::ProductVisionShape *>(s.get());
            QVERIFY(t);
            QCOMPARE(t->role, u->role);
            QCOMPARE(t->title, u->title);
            QCOMPARE(t->items, u->items);
        }
        board.selectAll();
        paints(board);
        board.paintSelection(QStringLiteral("#b3e5fc"));
        paints(board);
    }
};

QTEST_MAIN(TestDiagramMindmap)
#include "tst_diagram_mindmap.moc"
