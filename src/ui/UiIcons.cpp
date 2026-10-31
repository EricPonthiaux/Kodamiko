// SPDX-License-Identifier: GPL-3.0-only

#include "UiIcons.h"

#include <QApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOption>

#include <algorithm>
#include <cmath>

namespace {

// The icons are drawn on a 16-unit square.
constexpr qreal iconUnits = 16;

using Painter = void (*)(QPainter &);

// A line icon drawn at the size asked for, so that it stays sharp as the
// interface font enlarges it (UiMetrics::iconSize), with its pen widened
// in proportion.
class LineIconEngine : public QIconEngine {
public:
    LineIconEngine(QColor color, Painter draw)
        : color_(color)
        , draw_(draw)
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal ratio = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, ratio));
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State, qreal scale) override
    {
        QPixmap pixmap(size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        drawCentered(pixmap, size);
        if (mode != QIcon::Disabled)
            return pixmap;
        // Greyed as Qt greys the pictures of an icon.
        const QStyleOption option;
        return QApplication::style()->generatedIconPixmap(QIcon::Disabled, pixmap, &option);
    }

    QIconEngine *clone() const override { return new LineIconEngine(*this); }

private:
    // The icon on the 16-unit square, centered in `size`.
    void drawCentered(QPixmap &pixmap, const QSize &size) const
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal side = std::min(size.width(), size.height());
        painter.translate((size.width() - side) / 2, (size.height() - side) / 2);
        painter.scale(side / iconUnits, side / iconUnits);
        painter.setPen(QPen(color_, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        draw_(painter);
    }

    QColor color_;
    Painter draw_;
};

} // namespace

namespace UiIcons {

namespace {

QIcon draw(const QString &name, const QColor &color);

} // namespace

// Icons are redrawn with every theme change and indicator update: each
// one is drawn once per color.
QIcon icon(const QString &name, const QColor &color)
{
    static QHash<QString, QIcon> cache;
    const QString key = name + QLatin1Char('#') + QString::number(color.rgba(), 16);
    auto it = cache.constFind(key);
    if (it == cache.constEnd())
        it = cache.insert(key, draw(name, color));
    return it.value();
}

namespace {

// Each icon by name, drawn on 16 x 16 with the pen in the icon's color.
const QHash<QString, Painter> &painters()
{
    static const QHash<QString, Painter> table = {
        {QStringLiteral("terminal"),
         [](QPainter &p) {
             p.drawRoundedRect(QRectF(1.5, 2.5, 13, 11), 2.5, 2.5);
             p.drawPolyline(QPolygonF({QPointF(4.5, 6), QPointF(6.5, 8), QPointF(4.5, 10)}));
             p.drawLine(QPointF(8, 10.5), QPointF(11.5, 10.5));
         }},
        {QStringLiteral("code"),
         [](QPainter &p) {
             p.drawPolyline(QPolygonF({QPointF(5.5, 4), QPointF(1.8, 8), QPointF(5.5, 12)}));
             p.drawPolyline(QPolygonF({QPointF(10.5, 4), QPointF(14.2, 8), QPointF(10.5, 12)}));
             p.drawLine(QPointF(9, 3), QPointF(7, 13));
         }},
        {QStringLiteral("document"),
         [](QPainter &p) {
             p.drawPolygon(
                 QPolygonF({QPointF(3, 1.5), QPointF(9, 1.5), QPointF(13, 5.5), QPointF(13, 14.5), QPointF(3, 14.5)}));
             p.drawPolyline(QPolygonF({QPointF(9, 1.5), QPointF(9, 5.5), QPointF(13, 5.5)}));
             p.drawLine(QPointF(5.5, 8.5), QPointF(10.5, 8.5));
             p.drawLine(QPointF(5.5, 11), QPointF(9, 11));
         }},
        {QStringLiteral("split"),
         [](QPainter &p) {
             p.drawRoundedRect(QRectF(1.5, 2.5, 13, 11), 1.5, 1.5);
             p.drawLine(QPointF(8, 2.5), QPointF(8, 13.5));
         }},
        {QStringLiteral("preview"),
         [](QPainter &p) {
             QPainterPath eye;
             eye.moveTo(1, 8);
             eye.cubicTo(4.5, 2, 11.5, 2, 15, 8);
             eye.cubicTo(11.5, 14, 4.5, 14, 1, 8);
             p.drawPath(eye);
             p.drawEllipse(QPointF(8, 8), 2, 2);
         }},
        {QStringLiteral("diff"),
         [](QPainter &p) {
             p.drawLine(QPointF(8, 2.5), QPointF(8, 9));
             p.drawLine(QPointF(4.75, 5.75), QPointF(11.25, 5.75));
             p.drawLine(QPointF(4.75, 12.5), QPointF(11.25, 12.5));
         }},
        {QStringLiteral("history"),
         [](QPainter &p) {
             p.drawArc(QRectF(2, 2, 12, 12), 200 * 16, -320 * 16);
             p.drawPolyline(QPolygonF({QPointF(2, 3.5), QPointF(2.4, 7), QPointF(5.8, 6.2)}));
             p.drawPolyline(QPolygonF({QPointF(8, 5), QPointF(8, 8.2), QPointF(10.3, 9.6)}));
         }},
        {QStringLiteral("files"),
         [](QPainter &p) {
             QPainterPath folder;
             folder.moveTo(1.8, 4);
             folder.quadTo(1.8, 2.8, 3, 2.8);
             folder.lineTo(6, 2.8);
             folder.lineTo(7.5, 4.3);
             folder.lineTo(13, 4.3);
             folder.quadTo(14.2, 4.3, 14.2, 5.5);
             folder.lineTo(14.2, 12);
             folder.quadTo(14.2, 13.2, 13, 13.2);
             folder.lineTo(3, 13.2);
             folder.quadTo(1.8, 13.2, 1.8, 12);
             folder.closeSubpath();
             p.drawPath(folder);
         }},
        // The structure of a file: a symbol and the symbols it contains.
        {QStringLiteral("outline"),
         [](QPainter &p) {
             p.drawRoundedRect(QRectF(1.75, 1.75, 4.5, 4.5), 1.2, 1.2);
             p.drawRoundedRect(QRectF(9.25, 4.75, 5, 3.5), 1.2, 1.2);
             p.drawRoundedRect(QRectF(9.25, 10.25, 5, 3.5), 1.2, 1.2);
             QPainterPath links;
             links.moveTo(4, 6.25);
             links.lineTo(4, 12);
             links.lineTo(9.25, 12);
             links.moveTo(4, 6.5);
             links.lineTo(9.25, 6.5);
             p.drawPath(links);
         }},
        // A ring of eight dots, the large ones on the axes: the agent's own
        // neutral motif, not Anthropic's logo (which needs its permission).
        {QStringLiteral("claude"),
         [](QPainter &p) {
             p.setBrush(p.pen().color());
             p.setPen(Qt::NoPen);
             for (int i = 0; i < 8; ++i) {
                 const qreal angle = i * M_PI / 4;
                 const qreal orbit = 5;
                 const qreal radius = i % 2 ? 1.0 : 1.4;
                 p.drawEllipse(QPointF(8 + orbit * std::cos(angle), 8 + orbit * std::sin(angle)), radius, radius);
             }
         }},
        // A target: a ring, four ticks and its center.
        {QStringLiteral("locate"),
         [](QPainter &p) {
             p.drawEllipse(QRectF(3.5, 3.5, 9, 9));
             p.drawLine(QPointF(8, 1), QPointF(8, 3.5));
             p.drawLine(QPointF(8, 12.5), QPointF(8, 15));
             p.drawLine(QPointF(1, 8), QPointF(3.5, 8));
             p.drawLine(QPointF(12.5, 8), QPointF(15, 8));
             p.setBrush(p.pen().color());
             p.setPen(Qt::NoPen);
             p.drawEllipse(QPointF(8, 8), 1.6, 1.6);
         }},
        {QStringLiteral("search"),
         [](QPainter &p) {
             p.drawEllipse(QRectF(2.5, 2.5, 8, 8));
             p.drawLine(QPointF(9.3, 9.3), QPointF(13.5, 13.5));
         }},
        {QStringLiteral("find-in-files"),
         [](QPainter &p) {
             p.drawLine(QPointF(2, 3), QPointF(9, 3));
             p.drawLine(QPointF(2, 6.5), QPointF(6, 6.5));
             p.drawLine(QPointF(2, 10), QPointF(5, 10));
             p.drawEllipse(QRectF(7.5, 6.5, 5, 5));
             p.drawLine(QPointF(11.8, 11.8), QPointF(14, 14));
         }},
        // Go to File: a page with a folded corner, a magnifier over it.
        {QStringLiteral("find-file"),
         [](QPainter &p) {
             p.drawPolyline(QPolygonF({QPointF(11, 6.5), QPointF(11, 4.5), QPointF(8, 1.5), QPointF(2.5, 1.5),
                                       QPointF(2.5, 14.5), QPointF(6.5, 14.5)}));
             p.drawPolyline(QPolygonF({QPointF(7.5, 1.5), QPointF(7.5, 5), QPointF(11, 5)}));
             p.drawEllipse(QPointF(10.2, 10.2), 2.7, 2.7);
             p.drawLine(QPointF(12.2, 12.2), QPointF(14.5, 14.5));
         }},
        // Find in Files: a folder, a magnifier over it.
        {QStringLiteral("find-in-folder"),
         [](QPainter &p) {
             p.drawPolyline(QPolygonF({QPointF(6.5, 13.5), QPointF(1.5, 13.5), QPointF(1.5, 2.5), QPointF(5.5, 2.5),
                                       QPointF(7, 4.5), QPointF(14.5, 4.5), QPointF(14.5, 7)}));
             p.drawEllipse(QPointF(10.2, 10.2), 2.7, 2.7);
             p.drawLine(QPointF(12.2, 12.2), QPointF(14.5, 14.5));
         }},
        // Command palette: a bare prompt, ">_".
        {QStringLiteral("command"),
         [](QPainter &p) {
             p.drawPolyline(QPolygonF({QPointF(2.5, 4), QPointF(7, 8), QPointF(2.5, 12)}));
             p.drawLine(QPointF(8.5, 12.5), QPointF(13.5, 12.5));
         }},
        // A folder.
        {QStringLiteral("folder"),
         [](QPainter &p) {
             p.drawPolygon(QPolygonF({QPointF(1.5, 13.5), QPointF(1.5, 2.5), QPointF(5.5, 2.5), QPointF(7, 4.5),
                                      QPointF(14.5, 4.5), QPointF(14.5, 13.5)}));
         }},
        // Copy: two sheets, one over the other.
        {QStringLiteral("copy"),
         [](QPainter &p) {
             p.drawRoundedRect(QRectF(5.5, 5.5, 8, 8), 1.5, 1.5);
             p.drawPolyline(QPolygonF(
                 {QPointF(10.5, 3.5), QPointF(10.5, 2.5), QPointF(2.5, 2.5), QPointF(2.5, 10.5), QPointF(3.5, 10.5)}));
         }},
        // Previous and next: chevrons up and down, left and right.
        {QStringLiteral("up"),
         [](QPainter &p) { p.drawPolyline(QPolygonF({QPointF(4, 10), QPointF(8, 6), QPointF(12, 10)})); }},
        {QStringLiteral("down"),
         [](QPainter &p) { p.drawPolyline(QPolygonF({QPointF(4, 6), QPointF(8, 10), QPointF(12, 6)})); }},
        {QStringLiteral("left"),
         [](QPainter &p) { p.drawPolyline(QPolygonF({QPointF(10, 4), QPointF(6, 8), QPointF(10, 12)})); }},
        {QStringLiteral("right"),
         [](QPainter &p) { p.drawPolyline(QPolygonF({QPointF(6, 4), QPointF(10, 8), QPointF(6, 12)})); }},
        // Close: a cross.
        {QStringLiteral("close"),
         [](QPainter &p) {
             p.drawLine(QPointF(4, 4), QPointF(12, 12));
             p.drawLine(QPointF(12, 4), QPointF(4, 12));
         }},
        {QStringLiteral("plus"),
         [](QPainter &p) {
             p.drawLine(QPointF(8, 3), QPointF(8, 13));
             p.drawLine(QPointF(3, 8), QPointF(13, 8));
         }},
        {QStringLiteral("check"),
         [](QPainter &p) { p.drawPolyline(QPolygonF({QPointF(3.5, 8.5), QPointF(6.5, 11.5), QPointF(12.5, 4.5)})); }},
        {QStringLiteral("minus"), [](QPainter &p) { p.drawLine(QPointF(3, 8), QPointF(13, 8)); }},
        // Whitespace: the open box of a visible space, "␣".
        {QStringLiteral("whitespace"),
         [](QPainter &p) {
             p.drawPolyline(QPolygonF({QPointF(3, 7.5), QPointF(3, 11.5), QPointF(13, 11.5), QPointF(13, 7.5)}));
         }},
        // An arrow turning back.
        {QStringLiteral("discard"),
         [](QPainter &p) {
             QPainterPath path;
             path.moveTo(4, 6.5);
             path.lineTo(10, 6.5);
             path.cubicTo(13.5, 6.5, 13.5, 13, 10, 13);
             path.lineTo(6, 13);
             p.drawPath(path);
             p.drawPolyline(QPolygonF({QPointF(6.5, 4), QPointF(4, 6.5), QPointF(6.5, 9)}));
         }},
        {QStringLiteral("refresh"),
         [](QPainter &p) {
             p.drawArc(QRectF(3, 3, 10, 10), 30 * 16, 290 * 16);
             p.drawPolyline(QPolygonF({QPointF(13.2, 2.5), QPointF(13.2, 6), QPointF(9.7, 6)}));
         }},
        {QStringLiteral("branch"),
         [](QPainter &p) {
             p.drawEllipse(QRectF(3, 1.5, 3, 3));
             p.drawEllipse(QRectF(3, 11.5, 3, 3));
             p.drawEllipse(QRectF(10.5, 3.5, 3, 3));
             p.drawLine(QPointF(4.5, 4.5), QPointF(4.5, 11.5));
             QPainterPath path;
             path.moveTo(12, 6.5);
             path.cubicTo(12, 10, 4.5, 8, 4.5, 11.5);
             p.drawPath(path);
         }},
        // Two chevrons pointing at each other: everything folds to the middle.
        {QStringLiteral("collapse-all"),
         [](QPainter &p) {
             p.drawPolyline(QPolygonF({QPointF(4.5, 2.5), QPointF(8, 6), QPointF(11.5, 2.5)}));
             p.drawPolyline(QPolygonF({QPointF(4.5, 13.5), QPointF(8, 10), QPointF(11.5, 13.5)}));
         }},
        // Two chevrons pointing apart.
        {QStringLiteral("expand-all"),
         [](QPainter &p) {
             p.drawPolyline(QPolygonF({QPointF(4.5, 6), QPointF(8, 2.5), QPointF(11.5, 6)}));
             p.drawPolyline(QPolygonF({QPointF(4.5, 10), QPointF(8, 13.5), QPointF(11.5, 10)}));
         }},
        // Three boxes in a tree: the diagram laid out.
        {QStringLiteral("arrange"),
         [](QPainter &p) {
             p.drawRect(QRectF(5.5, 1.5, 5, 4));
             p.drawRect(QRectF(1.5, 10.5, 5, 4));
             p.drawRect(QRectF(9.5, 10.5, 5, 4));
             p.drawPolyline(QPolygonF({QPointF(4, 10.5), QPointF(4, 8), QPointF(12, 8), QPointF(12, 10.5)}));
             p.drawLine(QPointF(8, 5.5), QPointF(8, 8));
         }},
        // Four corners framing the view.
        {QStringLiteral("fit"),
         [](QPainter &p) {
             p.drawPolyline(QPolygonF({QPointF(2, 5.5), QPointF(2, 2), QPointF(5.5, 2)}));
             p.drawPolyline(QPolygonF({QPointF(10.5, 2), QPointF(14, 2), QPointF(14, 5.5)}));
             p.drawPolyline(QPolygonF({QPointF(14, 10.5), QPointF(14, 14), QPointF(10.5, 14)}));
             p.drawPolyline(QPolygonF({QPointF(5.5, 14), QPointF(2, 14), QPointF(2, 10.5)}));
             p.drawRect(QRectF(5.5, 5.5, 5, 5));
         }},
        // An arrow rising out of a tray.
        {QStringLiteral("export"),
         [](QPainter &p) {
             p.drawPolyline(
                 QPolygonF({QPointF(2.5, 9.5), QPointF(2.5, 13.5), QPointF(13.5, 13.5), QPointF(13.5, 9.5)}));
             p.drawLine(QPointF(8, 10), QPointF(8, 2));
             p.drawPolyline(QPolygonF({QPointF(4.8, 5), QPointF(8, 1.8), QPointF(11.2, 5)}));
         }},
        // Nine dots: the canvas's grid.
        {QStringLiteral("grid"),
         [](QPainter &p) {
             p.setPen(QPen(p.pen().color(), 2.4, Qt::SolidLine, Qt::RoundCap));
             p.drawPoints(QPolygonF({{3.5, 3.5},
                                     {8, 3.5},
                                     {12.5, 3.5},
                                     {3.5, 8},
                                     {8, 8},
                                     {12.5, 8},
                                     {3.5, 12.5},
                                     {8, 12.5},
                                     {12.5, 12.5}}));
         }},
        // A window, an arrow leaving it by its top right corner.
        {QStringLiteral("detach"),
         [](QPainter &p) {
             p.drawPolyline(QPolygonF(
                 {QPointF(7.5, 2.5), QPointF(2.5, 2.5), QPointF(2.5, 13.5), QPointF(13.5, 13.5), QPointF(13.5, 8.5)}));
             p.drawLine(QPointF(7.5, 8.5), QPointF(13.5, 2.5));
             p.drawPolyline(QPolygonF({QPointF(9.5, 2.5), QPointF(13.5, 2.5), QPointF(13.5, 6.5)}));
         }},
        // A window, an arrow entering it toward its bottom left corner.
        {QStringLiteral("attach"),
         [](QPainter &p) {
             p.drawPolyline(QPolygonF(
                 {QPointF(7.5, 2.5), QPointF(2.5, 2.5), QPointF(2.5, 13.5), QPointF(13.5, 13.5), QPointF(13.5, 8.5)}));
             p.drawLine(QPointF(13.5, 2.5), QPointF(6.5, 9.5));
             p.drawPolyline(QPolygonF({QPointF(6.5, 5.5), QPointF(6.5, 9.5), QPointF(10.5, 9.5)}));
         }},
        // A window with a panel on its right side.
        {QStringLiteral("side-panel"),
         [](QPainter &p) {
             p.drawRoundedRect(QRectF(1.5, 2.5, 13, 11), 1.5, 1.5);
             p.drawLine(QPointF(10, 2.5), QPointF(10, 13.5));
         }},
        // A window with a panel on its left side.
        {QStringLiteral("file-list"),
         [](QPainter &p) {
             p.drawRoundedRect(QRectF(1.5, 2.5, 13, 11), 1.5, 1.5);
             p.drawLine(QPointF(6, 2.5), QPointF(6, 13.5));
         }},
        // A funnel.
        {QStringLiteral("filter"),
         [](QPainter &p) {
             QPainterPath funnel;
             funnel.moveTo(2, 3);
             funnel.lineTo(14, 3);
             funnel.lineTo(9.5, 8.5);
             funnel.lineTo(9.5, 13.5);
             funnel.lineTo(6.5, 12);
             funnel.lineTo(6.5, 8.5);
             funnel.closeSubpath();
             p.drawPath(funnel);
         }},
        // Three dots, one above the other.
        {QStringLiteral("more"),
         [](QPainter &p) {
             p.setBrush(p.pen().color());
             p.setPen(Qt::NoPen);
             for (const qreal y : {3.5, 8.0, 12.5})
                 p.drawEllipse(QPointF(8, y), 1.4, 1.4);
         }},
        // A disc, half filled: light and dark.
        {QStringLiteral("appearance"),
         [](QPainter &p) {
             p.drawEllipse(QRectF(2, 2, 12, 12));
             QPainterPath half;
             half.moveTo(8, 2);
             half.arcTo(QRectF(2, 2, 12, 12), 90, 180);
             half.closeSubpath();
             p.setBrush(p.pen().color());
             p.drawPath(half);
         }},
        // A capital A: the fonts.
        {QStringLiteral("fonts"),
         [](QPainter &p) {
             p.drawPolyline(QPolygonF({QPointF(3, 13.5), QPointF(8, 2.5), QPointF(13, 13.5)}));
             p.drawLine(QPointF(5, 9.5), QPointF(11, 9.5));
         }},
        {QStringLiteral("keyboard"),
         [](QPainter &p) {
             p.drawRoundedRect(QRectF(1, 3.5, 14, 9), 2, 2);
             p.setBrush(p.pen().color());
             p.setPen(Qt::NoPen);
             for (int i = 0; i < 4; ++i) {
                 p.drawEllipse(QPointF(4 + i * 2.7, 6.5), 0.8, 0.8);
                 if (i == 0 || i == 3)
                     p.drawEllipse(QPointF(4 + i * 2.7, 9.5), 0.8, 0.8);
             }
             p.drawRoundedRect(QRectF(6, 8.8, 4, 1.4), 0.7, 0.7);
         }},
        // A gear: a ring and eight teeth.
        {QStringLiteral("settings"),
         [](QPainter &p) {
             p.drawEllipse(QPointF(8, 8), 2.2, 2.2);
             p.drawEllipse(QPointF(8, 8), 4.6, 4.6);
             for (int i = 0; i < 8; ++i) {
                 const qreal angle = i * M_PI / 4;
                 p.drawLine(QPointF(8 + 4.6 * std::cos(angle), 8 + 4.6 * std::sin(angle)),
                            QPointF(8 + 6.6 * std::cos(angle), 8 + 6.6 * std::sin(angle)));
             }
         }},
        // Play: a rounded triangle.
        {QStringLiteral("run"),
         [](QPainter &p) { p.drawPolygon(QPolygonF({QPointF(4.5, 2.8), QPointF(13, 8), QPointF(4.5, 13.2)})); }},
        {QStringLiteral("stop"), [](QPainter &p) { p.drawRoundedRect(QRectF(3.5, 3.5, 9, 9), 1.5, 1.5); }},
        // A hammer: its head, then its handle.
        {QStringLiteral("build"),
         [](QPainter &p) {
             p.drawPolygon(QPolygonF({QPointF(3, 4.5), QPointF(6.5, 1.8), QPointF(10.5, 5.8), QPointF(8.6, 7.7),
                                      QPointF(6.4, 5.5), QPointF(4.8, 6.3)}));
             p.drawLine(QPointF(7.5, 6.6), QPointF(13.8, 12.9));
         }},
        // A bug: its body, its head, three legs on each side.
        {QStringLiteral("debug"),
         [](QPainter &p) {
             p.drawEllipse(QPointF(8, 9.4), 3.6, 4.6);
             p.drawArc(QRectF(5.6, 2.4, 4.8, 4.4), 0, 180 * 16);
             p.drawLines(QList<QLineF>{{QPointF(1.8, 6.2), QPointF(4.4, 7)},
                                       {QPointF(1.8, 8.8), QPointF(4.4, 9.6)},
                                       {QPointF(1.8, 11.4), QPointF(4.4, 12.2)},
                                       {QPointF(11.6, 7), QPointF(14.2, 6.2)},
                                       {QPointF(11.6, 9.6), QPointF(14.2, 8.8)},
                                       {QPointF(11.6, 12.2), QPointF(14.2, 11.4)}});
         }},
        // A flask.
        {QStringLiteral("test"),
         [](QPainter &p) {
             p.drawPolyline(QPolygonF({QPointF(6.2, 2), QPointF(6.2, 6.2), QPointF(2.6, 12.6), QPointF(3.6, 14),
                                       QPointF(12.4, 14), QPointF(13.4, 12.6), QPointF(9.8, 6.2), QPointF(9.8, 2)}));
             p.drawLine(QPointF(5, 2), QPointF(11, 2));
             p.drawLine(QPointF(4.6, 10.2), QPointF(11.4, 10.2));
         }},
        // A warning sign.
        {QStringLiteral("problems"),
         [](QPainter &p) {
             p.drawPolygon(QPolygonF({QPointF(8, 2), QPointF(14.5, 13.5), QPointF(1.5, 13.5)}));
             p.drawLine(QPointF(8, 6.2), QPointF(8, 9.2));
             p.drawPoint(QPointF(8, 11.4));
         }},
    };
    return table;
}

// Command palette: a prompt.
void drawPrompt(QPainter &p)
{
    p.drawRoundedRect(QRectF(1.5, 2.5, 13, 11), 2, 2);
    p.drawPolyline(QPolygonF({QPointF(4.5, 6), QPointF(6.5, 8), QPointF(4.5, 10)}));
    p.drawLine(QPointF(8, 10), QPointF(11, 10));
}

QIcon draw(const QString &name, const QColor &color)
{
    return QIcon(new LineIconEngine(color, painters().value(name, &drawPrompt))); // owned by the icon
}

} // namespace

} // namespace UiIcons
