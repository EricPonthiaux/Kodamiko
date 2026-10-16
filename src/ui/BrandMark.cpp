#include "BrandMark.h"

#include <QPainter>
#include <QPainterPath>

namespace BrandMark {

void paint(QPainter *painter, const QRectF &area, const QColor &caret, const QColor &block)
{
    // The shapes on a 64-unit grid, as in packaging/brand/duet-mark.svg.
    const qreal unit = std::min(area.width(), area.height()) / 64.0;
    const QPointF origin = area.center() - QPointF(32 * unit, 32 * unit);
    const auto rect = [&](qreal x, qreal y, qreal w, qreal h) {
        return QRectF(origin.x() + x * unit, origin.y() + y * unit, w * unit, h * unit);
    };
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(caret);
    painter->drawRoundedRect(rect(17, 12, 7, 40), 3.5 * unit, 3.5 * unit);
    painter->setBrush(block);
    painter->drawRoundedRect(rect(31, 24, 16, 28), 4 * unit, 4 * unit);
    painter->restore();
}

void paintAppIcon(QPainter *painter, const QRectF &area)
{
    const qreal size = std::min(area.width(), area.height());
    const QRectF tile(area.center() - QPointF(size / 2, size / 2), QSizeF(size, size));
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(tileColor);
    painter->drawRoundedRect(tile, size * 0.22, size * 0.22);
    painter->restore();
    // The mark fills two thirds of the tile.
    const qreal mark = size * 0.68;
    paint(painter, QRectF(tile.center() - QPointF(mark / 2, mark / 2), QSizeF(mark, mark)));
}

QPixmap appIcon(int size, qreal devicePixelRatio)
{
    QPixmap pixmap(QSize(size, size) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    paintAppIcon(&painter, QRectF(0, 0, size, size));
    return pixmap;
}

QIcon windowIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addPixmap(appIcon(size));
    return icon;
}

} // namespace BrandMark
