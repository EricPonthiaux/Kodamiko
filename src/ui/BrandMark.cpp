// SPDX-License-Identifier: GPL-3.0-only

#include "BrandMark.h"

#include <QPainter>
#include <QPainterPath>

namespace BrandMark {

namespace {

// The rectangle (`x`, `y`, `w`, `h`) of the 64-unit grid of
// packaging/brand/duet-mark.svg, fitted in the square `area`.
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
    const auto rect = [&](qreal x, qreal y, qreal w, qreal h) { return gridRect(area, x, y, w, h); };
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(colors.caret);
    painter->drawRoundedRect(rect(17, 12, 7, 40), 3.5 * unit, 3.5 * unit);
    painter->setBrush(colors.block);
    painter->drawRoundedRect(rect(31, 24, 16, 28), 4 * unit, 4 * unit);
    painter->restore();
}

QRectF inkRect(const QRectF &area)
{
    // From the caret's left to the block's right, the shapes' tops to bottoms.
    return gridRect(area, 17, 12, 30, 40);
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
    // The mark fills two thirds of the tile.
    const qreal mark = size * 0.68;
    paint(painter, QRectF(tile.center() - QPointF(mark / 2, mark / 2), QSizeF(mark, mark)), colorsFor(theme));
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
