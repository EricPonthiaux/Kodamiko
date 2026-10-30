// SPDX-License-Identifier: GPL-3.0-only

#include "ui/Theme.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Canvas.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/Graphviz.h"
#include "viewers/diagram/families/Families.h"
#include "viewers/diagram/families/states/States.h"

#include <QImage>
#include <QPainter>
#include <QtTest>

#include <algorithm>
#include <cmath>

using namespace diagram;
using namespace diagram::states;

namespace {

qreal luminance(const QColor &color)
{
    const auto linear = [](qreal value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

qreal contrast(const QColor &a, const QColor &b)
{
    const qreal x = luminance(a);
    const qreal y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

QImage paintShape(const Shape &shape, const Board &board, const ShapeState &state = {})
{
    QImage image(qCeil(shape.w) + 16, qCeil(shape.h) + 16, QImage::Format_ARGB32_Premultiplied);
    image.fill(board.style().background);
    QPainter painter(&image);
    painter.translate(8 - shape.x, 8 - shape.y);
    shape.draw(painter, board, state);
    return image;
}

void build(Board &board, const QString &source)
{
    const Codec &codec = *families::stateCodecs().first();
    Model model;
    QString error;
    QVERIFY2(codec.parse(source, model, &error), qPrintable(error));
    QVERIFY(model.warnings.isEmpty());
    buildBoard(board, codec, model);
    board.measureAll();
    QVERIFY(graphviz::autoArrange(board, {QStringLiteral("LR"), {}, {}, true}));
}

QRectF labelRect(const Link &link, const Board &board)
{
    const auto box = link.labelBox(board);
    return {link.labelPos(*link.route(board), board) - QPointF(box.width / 2, box.height / 2),
            QSizeF(box.width, box.height)};
}

} // namespace

class TestDiagramStatesRendering : public QObject {
    Q_OBJECT

private slots:

    void themesKeepHeadersDescriptionsAndSymbolsReadable_data()
    {
        QTest::addColumn<QString>("themeName");
        for (const Theme &theme : Theme::all())
            QTest::newRow(qPrintable(theme.name)) << theme.name;
        QTest::newRow("print") << QStringLiteral("print");
    }

    void themesKeepHeadersDescriptionsAndSymbolsReadable()
    {
        QFETCH(const QString, themeName);
        Board board;
        board.setFamily(&families::states());
        const Shape *state = board.addShape({}, {{"text", "Checking"}, {"desc", QJsonArray{"entry / runChecks()"}}});
        const QByteArray original = board.snapshot();
        board.setStyle(themeName == QLatin1String("print") ? Style::printed()
                                                           : Style::fromTheme(Theme::named(themeName)));
        const auto &colors = board.style().state;
        const QImage image = paintShape(*state, board);
        const QColor header = image.pixelColor(20, 20);
        const QColor body = image.pixelColor(20, qRound(state->h) + 2);
        QVERIFY(header != body);
        QVERIFY(contrast(colors.ink, header) >= 4.5);
        QVERIFY(contrast(colors.mutedInk, body) >= 4.5);
        QVERIFY(contrast(colors.ink, colors.containerHeader) >= 4.5);
        QVERIFY(contrast(colors.line, colors.container) >= 3);
        QVERIFY(contrast(colors.marker, colors.container) >= 3);
        QCOMPARE(board.snapshot(), original);
    }

    void customFillsRemainEditableAndSelected()
    {
        Board board;
        board.setFamily(&families::states());
        TextEditSpec editor;
        board.setTextEditor([&editor](Shape *, Link *, const TextEditSpec &spec) { editor = spec; });
        for (const QString &type :
             {QStringLiteral("state"), QStringLiteral("stateContainer"), QStringLiteral("choice")}) {
            Shape *shape = board.addShape(type, {{"text", "Custom"}});
            board.selectShape(shape->id);
            QVERIFY(board.paintSelection(QStringLiteral("#ffe1b6")));
            for (const Theme &theme : Theme::all()) {
                board.setStyle(Style::fromTheme(theme));
                shape->onDoubleClick(board);
                QVERIFY(contrast(editor.color, QColor(QStringLiteral("#ffe1b6"))) >= 4.5);
                const QImage image = paintShape(*shape, board, {.selected = true});
                QCOMPARE(image.pixelColor(8, 8 + qRound(shape->h / 2)), theme.accent);
            }
        }
    }

    void legacyFillAndDescriptionUseTheSameReadableInk()
    {
        Board board;
        board.setFamily(&families::states());
        Shape *shape = board.addShape({}, {{"text", "Custom"},
                                           {"fill", "#ffe1b6"},
                                           {"desc", QJsonArray{"entry / openEditor()", "do / collectChanges()"}}});
        TextEditSpec editor;
        board.setTextEditor([&editor](Shape *, Link *, const TextEditSpec &spec) { editor = spec; });
        shape->onDoubleClick(board);
        QVERIFY(editor.font.mono);
        QVERIFY(contrast(editor.color, QColor(QStringLiteral("#ffe1b6"))) >= 4.5);
        QCOMPARE(paintShape(*shape, board).pixelColor(20, 20), QColor(QStringLiteral("#ffe1b6")));
    }

    void initialAndFinalStatesKeepTheirDistinctSymbols()
    {
        Board board;
        board.setFamily(&families::states());
        const Shape *initial = board.addShape(QStringLiteral("initial"), {});
        const Shape *final = board.addShape(QStringLiteral("final"), {});
        const QImage dot = paintShape(*initial, board);
        const QImage ring = paintShape(*final, board);
        QCOMPARE(dot.pixelColor(8 + qRound(initial->w / 2), 8 + qRound(initial->h / 2)).rgba(),
                 board.style().state.marker.rgba());
        QCOMPARE(ring.pixelColor(8 + qRound(final->w / 2), 8 + qRound(final->h / 2)).rgba(),
                 board.style().state.marker.rgba());
        QCOMPARE(ring.pixelColor(8 + qRound(final->w / 2) + 8, 8 + qRound(final->h / 2)), board.style().background);
        for (const QString &type :
             {QStringLiteral("initial"), QStringLiteral("final"), QStringLiteral("fork"), QStringLiteral("join")}) {
            const Shape *shape = board.addShape(type, {});
            board.selectShape(shape->id);
            QVERIFY(board.paintSelection(QStringLiteral("#ffe1b6")));
            const QImage selected = paintShape(*shape, board, {.selected = true});
            QCOMPARE(selected.pixelColor(8 + qRound(shape->w / 2), 8 + qRound(shape->h / 2)),
                     board.style().shape.selectedStroke);
        }
    }

    void compositeTitlesLeaveRoomForTheirChildren()
    {
        Board board;
        build(board,
              QStringLiteral(
                  "stateDiagram-v2\nstate Review {\n[*] --> Checking\n"
                  "state Validation {\n[*] --> Ready\nReady --> [*]\n}\nChecking --> Validation\n}\nReview --> Done"));
        for (const auto &shape : board.shapes()) {
            if (const Shape *parent = board.shapeByReference(shape->parentBoundary))
                QVERIFY2(parent->childBounds(board)->contains(shape->rect()), qPrintable(shape->key));
        }
    }

    void transitionLabelsReserveTheirActualSizeAndStayEditable()
    {
        Board board;
        build(board,
              QStringLiteral("stateDiagram-v2\nReady --> Done : submit [checksPassed] / closeWindow()\\nnotifySuccess()"
                             "\nReady --> Ready : retry\\n[attempts < 3]"));
        TextEditSpec editor;
        board.setTextEditor([&editor](Shape *, Link *, const TextEditSpec &spec) { editor = spec; });
        for (const auto &link : board.links()) {
            const QRectF rect = labelRect(*link, board);
            QVERIFY(link->hitTest(board, rect.center(), 2));
            QVERIFY(link->bounds(board)->contains(rect));
            link->onDoubleClick(board);
            QVERIFY(editor.font.mono);
            QVERIFY(editor.rect().contains(rect));
            for (const auto &shape : board.shapes())
                QVERIFY2(!shape->rect().intersects(rect), qPrintable(link->label + QLatin1Char('/') + shape->key));
        }
        Link &link = *board.links().front();
        link.customColor = QStringLiteral("#af3379");
        link.customWidth = 4;
        QCOMPARE(link.drawStyle(board, false).color, QColor(QStringLiteral("#af3379")));
        QCOMPARE(link.drawStyle(board, false).width, 4.0);
        QCOMPARE(link.drawStyle(board, true).markerScale, link.drawStyle(board, false).markerScale);
    }

    void compoundExitLabelsDoNotCoverTheFrameOrTarget_data()
    {
        QTest::addColumn<bool>("reservedRoute");
        QTest::newRow("reserved-label") << true;
        QTest::newRow("frame-anchor") << false;
    }

    void compoundExitLabelsDoNotCoverTheFrameOrTarget()
    {
        QFETCH(const bool, reservedRoute);
        Board board;
        board.setFamily(&families::states());
        const Shape *parent = board.addShape(QStringLiteral("stateContainer"), {{"text", "Review"}});
        const Shape *target = board.addShape({}, {{"text", "Merged"}, {"x", 100}, {"y", parent->h + 24}});
        Link *exit = board.addLink({}, parent->id, target->id, {{"label", "merge"}});
        const QPointF start(target->cx(), parent->y + parent->h);
        const QPointF end(target->cx(), target->y);
        if (reservedRoute) {
            exit->spline = Spline{.points = {start, start + QPointF(0, 8), end - QPointF(0, 8), end},
                                  .labelPoint = (start + end) / 2,
                                  .fromAnchor = {parent->x, parent->y},
                                  .toAnchor = {target->x, target->y}};
        }
        board.resolveLabels();
        const QRectF label = labelRect(*exit, board);
        QVERIFY(!label.intersects(parent->rect()));
        QVERIFY(!label.intersects(target->rect()));
        QVERIFY(!label.contains((start + end) / 2));
        QVERIFY(exit->bounds(board)->contains(label));
    }
};

QTEST_MAIN(TestDiagramStatesRendering)
#include "tst_diagram_states_rendering.moc"
