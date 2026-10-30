// SPDX-License-Identifier: GPL-3.0-only

#include "BrandMark.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace BrandMark {

namespace {

// The rectangle (`x`, `y`, `w`, `h`) of the 64-unit grid of
// packaging/brand/kodamiko-mark.svg, fitted in the square `area`.
QRectF gridRect(const QRectF &area, qreal x, qreal y, qreal w, qreal h)
{
    const qreal unit = std::min(area.width(), area.height()) / 64.0;
    const QPointF origin = area.center() - QPointF(32 * unit, 32 * unit);
    return {origin.x() + x * unit, origin.y() + y * unit, w * unit, h * unit};
}

} // namespace

void paint(QPainter *painter, const QRectF &area, const Colors &colors)
{
    const qreal unit = std::min(area.width(), area.height()) / 64.0;
    const QPointF origin = gridRect(area, 0, 0, 64, 64).topLeft();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(origin);
    painter->scale(unit, unit);
    painter->setPen(Qt::NoPen);

    // The upright keeps the editor's caret; the two cut strokes complete a K.
    QPainterPath caret;
    caret.moveTo(14, 12);
    caret.lineTo(22, 12);
    caret.lineTo(22, 52);
    caret.lineTo(14, 52);
    caret.cubicTo(12.9, 52, 12, 51.1, 12, 50);
    caret.lineTo(12, 14);
    caret.cubicTo(12, 12.9, 12.9, 12, 14, 12);
    caret.closeSubpath();
    painter->setBrush(colors.caret);
    painter->drawPath(caret);

    QPainterPath arms;
    arms.moveTo(26, 29);
    arms.lineTo(43, 12);
    arms.lineTo(55, 12);
    arms.lineTo(36, 31);
    arms.lineTo(26, 31);
    arms.closeSubpath();
    arms.moveTo(26, 35);
    arms.lineTo(36, 35);
    arms.lineTo(53, 52);
    arms.lineTo(41, 52);
    arms.lineTo(26, 37);
    arms.closeSubpath();
    painter->setBrush(colors.block);
    painter->drawPath(arms);
    painter->restore();
}

QRectF inkRect(const QRectF &area)
{
    // The visible bounds of the caret and the two diagonal strokes.
    return gridRect(area, 12, 12, 43, 40);
}

Colors colorsFor(const Theme &theme)
{
    return {.caret = theme.foreground, .block = theme.accent};
}

QPixmap markPixmap(int size, const Theme &theme, qreal devicePixelRatio)
{
    QPixmap pixmap(QSize(size, size) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    paint(&painter, QRectF(0, 0, size, size), colorsFor(theme));
    return pixmap;
}

QRectF centredArea(const QRectF &square, qreal inkShare)
{
    const QRectF unitInk = inkRect(QRectF(0, 0, 1, 1));
    const qreal side = square.height() * inkShare / unitInk.height();
    return {square.center() - unitInk.center() * side, QSizeF(side, side)};
}

void paintAppIcon(QPainter *painter, const QRectF &area, const Theme &theme)
{
    const qreal size = std::min(area.width(), area.height());
    const QRectF tile(area.center() - QPointF(size / 2, size / 2), QSizeF(size, size));
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(theme.window);
    painter->drawRoundedRect(tile, size * 0.22, size * 0.22);
    painter->restore();
    // The monogram occupies 64 % of the tile height.
    paint(painter, centredArea(tile, 0.64), colorsFor(theme));
}

QPixmap appIcon(int size, const Theme &theme, qreal devicePixelRatio)
{
    QPixmap pixmap(QSize(size, size) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    paintAppIcon(&painter, QRectF(0, 0, size, size), theme);
    return pixmap;
}

QIcon windowIcon(const Theme &theme)
{
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addPixmap(appIcon(size, theme));
    return icon;
}

} // namespace BrandMark
