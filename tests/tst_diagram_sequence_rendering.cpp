// SPDX-License-Identifier: GPL-3.0-only

#include "ui/Theme.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Canvas.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/families/Families.h"
#include "viewers/diagram/families/sequence/Lanes.h"
#include "viewers/diagram/families/sequence/Sequence.h"

#include <QImage>
#include <QPainter>
#include <QtTest>

#include <algorithm>
#include <cmath>

using namespace diagram;
using namespace diagram::sequence;

namespace {

void build(Board &board, const QString &source)
{
    const Codec &codec = *families::sequenceCodecs().first();
    Model model;
    QString error;
    QVERIFY2(codec.parse(source, model, &error), qPrintable(error));
    QVERIFY(model.warnings.isEmpty());
    buildBoard(board, codec, model);
    board.measureAll();
    arrange(board);
}

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

QRectF labelRect(const Link &message, const Board &board)
{
    const auto box = message.labelBox(board);
    const QPointF center = message.labelPos(*message.route(board), board);
    return {center - QPointF(box.width / 2, box.height / 2), QSizeF(box.width, box.height)};
}

QImage paint(const Board &board)
{
    QImage image(1800, 1600, QImage::Format_ARGB32_Premultiplied);
    image.fill(board.style().background);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    Canvas::paintBoard(painter, board);
    return image;
}

} // namespace

class TestDiagramSequenceRendering : public QObject {
    Q_OBJECT

private slots:

    void themesKeepTextReadableAndContentIntact_data()
    {
        QTest::addColumn<QString>("themeName");
        for (const Theme &theme : Theme::all())
            QTest::newRow(qPrintable(theme.name)) << theme.name;
        QTest::newRow("print") << QStringLiteral("print");
    }

    void themesKeepTextReadableAndContentIntact()
    {
        QFETCH(const QString, themeName);
        Board board;
        build(board, QStringLiteral("sequenceDiagram\nA->>+B: inspectChanges()\nalt valid\nB-->>-A: done\nend"));
        const QByteArray original = board.snapshot();
        board.setStyle(themeName == QLatin1String("print") ? Style::printed()
                                                           : Style::fromTheme(Theme::named(themeName)));
        const auto &colors = board.style().sequence;
        const Shape &header = *board.shapes().front();
        const QImage image = paint(board);
        QCOMPARE(image.pixelColor(qRound(header.x + 12), qRound(header.y + 12)).rgba(), colors.header.rgba());
        QVERIFY(contrast(colors.ink, colors.header) >= 4.5);
        QVERIFY(contrast(colors.ink, board.style().background) >= 4.5);
        QVERIFY(contrast(colors.mutedInk, colors.fragment) >= 4.5);
        QVERIFY(contrast(colors.line, colors.fragment) >= 3);
        QCOMPARE(board.snapshot(), original);
    }

    void customFillsKeepReadableTextAndSelection()
    {
        Board board;
        build(board, QStringLiteral("sequenceDiagram\nA->>+B: work\nB-->>-A: done"));
        Shape *header = board.shapes().front().get();
        board.selectShape(header->id);
        QVERIFY(board.paintSelection(QStringLiteral("#ffe1b6")));
        TextEditSpec editor;
        board.setTextEditor([&editor](Shape *, Link *, const TextEditSpec &spec) { editor = spec; });
        for (const Theme &theme : Theme::all()) {
            board.setStyle(Style::fromTheme(theme));
            header->onDoubleClick(board);
            QVERIFY(contrast(editor.color, QColor(QStringLiteral("#ffe1b6"))) >= 4.5);
            QVERIFY(editor.font.weight >= 600);
            QImage image(400, 200, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.translate(8, 0);
            header->draw(painter, board, {.selected = true});
            painter.end();
            QCOMPARE(image.pixelColor(qRound(header->x + 8), qRound(header->cy())), theme.accent);
        }
    }

    void legacyFillUsesTheSameContrastAsTheEditor()
    {
        Board board;
        board.setFamily(&families::sequence());
        Shape *header = board.addShape({}, {{"text", "Painted"}, {"fill", "#ffe1b6"}});
        TextEditSpec editor;
        board.setTextEditor([&editor](Shape *, Link *, const TextEditSpec &spec) { editor = spec; });
        header->onDoubleClick(board);
        QVERIFY(contrast(editor.color, QColor(QStringLiteral("#ffe1b6"))) >= 4.5);
        QCOMPARE(paint(board).pixelColor(12, 12), QColor(QStringLiteral("#ffe1b6")));
    }

    void multilineMessagesFitAndStayEditable()
    {
        Board board;
        build(board, QStringLiteral("sequenceDiagram\nA->>B: Short<br/>A much longer signature(parameter, option)"
                                    "\nB->>B: Self<br/>Several<br/>Lines\nB-->>A: Done"));
        TextEditSpec editor;
        board.setTextEditor([&editor](Shape *, Link *, const TextEditSpec &spec) { editor = spec; });
        for (const auto &link : board.links()) {
            const QRectF label = labelRect(*link, board);
            QVERIFY(link->hitTest(board, label.center(), 2));
            QVERIFY(link->bounds(board)->contains(label));
            link->onDoubleClick(board);
            QVERIFY(editor.font.mono);
            QVERIFY(editor.rect().contains(label));
            QCOMPARE(editor.rect().center(), label.center());
            QVERIFY(label.width() >= text::widest(editor.font, link->label.split(QLatin1Char('\n'))));
        }
        const QRectF first = labelRect(*board.links()[0], board);
        QVERIFY(first.left() > board.shapes()[0]->cx());
        QVERIFY(first.right() < board.shapes()[1]->cx());
        const QRectF self = labelRect(*board.links()[1], board);
        const QRectF last = labelRect(*board.links()[2], board);
        QVERIFY(self.bottom() < last.top());
    }

    void arrowsMeetActivationEdgesWithoutChangingTheirMarkers()
    {
        Board board;
        build(board, QStringLiteral("sequenceDiagram\nA->>+B: start\nB<<-->>-A: done"));
        const Shape &active = *board.shapes()[1];
        const Route call = *board.links()[0]->route(board);
        const Route reply = *board.links()[1]->route(board);
        QCOMPARE(call.b.x(), active.cx() - 5);
        QCOMPARE(reply.a.x(), active.cx() - 5);
        Link &link = *board.links()[1];
        link.customColor = QStringLiteral("#af3379");
        link.customWidth = 4;
        link.customLineStyle = QStringLiteral("dotted");
        QCOMPARE(link.drawStyle(board, false).color, QColor(QStringLiteral("#af3379")));
        QCOMPARE(link.drawStyle(board, false).width, 4.0);
        QCOMPARE(link.drawStyle(board, true).markerScale, link.drawStyle(board, false).markerScale);
        QCOMPARE(link.toJson().value(QStringLiteral("startHead")).toString(), QStringLiteral("arrow"));
        QCOMPARE(linkDash(link), (QList<qreal>{2, 5}));
    }

    void nestedFragmentsAndLongGuardsStayInsideExportBounds()
    {
        Board board;
        build(board, QStringLiteral("sequenceDiagram\nparticipant A\nloop Retry while the service is unavailable"
                                    " and the retry budget has not been exhausted\nalt ready\nA->>A: work()"
                                    "\nelse waiting\nA->>A: wait()\nend\nend\nA->>A: finish()"));
        const auto frames = detail::fragmentBounds(board);
        QVERIFY(frames);
        QVERIFY(board.contentBounds().contains(*frames));
        const QRectF last = labelRect(*board.links().back(), board);
        QVERIFY(last.top() > frames->bottom());
        const auto *first = static_cast<MessageLink *>(board.links().front().get());
        QVERIFY(detail::fragmentHeaderHeight(board, first->fragments.first()) > 36);
        QVERIFY(paint(board).size().isValid());
    }

    void nestedSelfCallsJoinTheMatchingActivation()
    {
        Board board;
        build(board, QStringLiteral("sequenceDiagram\nA->>+B: start\nB->>+B: nested"
                                    "\nB-->>-B: nested result\nB-->>-A: done"));
        const qreal center = board.shapes()[1]->cx();
        const Route call = *board.links()[1]->route(board);
        const Route reply = *board.links()[2]->route(board);
        QCOMPARE(call.a.x(), center + 5);
        QCOMPARE(call.b.x(), center + 9);
        QCOMPARE(reply.a.x(), center + 9);
        QCOMPARE(reply.b.x(), center + 5);
    }
};

QTEST_MAIN(TestDiagramSequenceRendering)
#include "tst_diagram_sequence_rendering.moc"
