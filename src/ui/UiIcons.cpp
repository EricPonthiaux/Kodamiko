#include "UiIcons.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <functional>

namespace {

QIcon drawIcon(const QColor &color, const std::function<void(QPainter &)> &draw)
{
    QIcon icon;
    for (const qreal ratio : {1.0, 2.0}) {
        QPixmap pixmap(QSize(16, 16) * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        draw(painter);
        icon.addPixmap(pixmap);
    }
    return icon;
}

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

QIcon draw(const QString &name, const QColor &color)
{
    if (name == QLatin1String("terminal")) {
        return drawIcon(color, [](QPainter &p) {
            p.drawRoundedRect(QRectF(1.5, 2.5, 13, 11), 2.5, 2.5);
            p.drawPolyline(QPolygonF({QPointF(4.5, 6), QPointF(6.5, 8), QPointF(4.5, 10)}));
            p.drawLine(QPointF(8, 10.5), QPointF(11.5, 10.5));
        });
    }
    if (name == QLatin1String("code")) {
        return drawIcon(color, [](QPainter &p) {
            p.drawPolyline(QPolygonF({QPointF(5.5, 4), QPointF(1.8, 8), QPointF(5.5, 12)}));
            p.drawPolyline(QPolygonF({QPointF(10.5, 4), QPointF(14.2, 8), QPointF(10.5, 12)}));
            p.drawLine(QPointF(9, 3), QPointF(7, 13));
        });
    }
    if (name == QLatin1String("diff")) {
        return drawIcon(color, [](QPainter &p) {
            p.drawLine(QPointF(8, 2.5), QPointF(8, 9));
            p.drawLine(QPointF(4.75, 5.75), QPointF(11.25, 5.75));
            p.drawLine(QPointF(4.75, 12.5), QPointF(11.25, 12.5));
        });
    }
    if (name == QLatin1String("history")) {
        return drawIcon(color, [](QPainter &p) {
            p.drawArc(QRectF(2, 2, 12, 12), 200 * 16, -320 * 16);
            p.drawPolyline(QPolygonF({QPointF(2, 3.5), QPointF(2.4, 7), QPointF(5.8, 6.2)}));
            p.drawPolyline(QPolygonF({QPointF(8, 5), QPointF(8, 8.2), QPointF(10.3, 9.6)}));
        });
    }
    if (name == QLatin1String("files")) {
        return drawIcon(color, [](QPainter &p) {
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
        });
    }
    if (name == QLatin1String("outline")) {
        // The structure of a file: a symbol and the symbols it contains.
        return drawIcon(color, [](QPainter &p) {
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
        });
    }
    if (name == QLatin1String("claude")) {
        // A burst: eight rays, as in Claude's mark.
        return drawIcon(color, [](QPainter &p) {
            for (int i = 0; i < 8; ++i) {
                const qreal angle = i * M_PI / 4;
                const qreal inner = 1.8;
                const qreal outer = i % 2 ? 5.2 : 6.3;
                p.drawLine(QPointF(8 + inner * std::cos(angle), 8 + inner * std::sin(angle)),
                           QPointF(8 + outer * std::cos(angle), 8 + outer * std::sin(angle)));
            }
        });
    }
    if (name == QLatin1String("locate")) {
        // A target: a ring, four ticks and its center.
        return drawIcon(color, [](QPainter &p) {
            p.drawEllipse(QRectF(3.5, 3.5, 9, 9));
            p.drawLine(QPointF(8, 1), QPointF(8, 3.5));
            p.drawLine(QPointF(8, 12.5), QPointF(8, 15));
            p.drawLine(QPointF(1, 8), QPointF(3.5, 8));
            p.drawLine(QPointF(12.5, 8), QPointF(15, 8));
            p.setBrush(p.pen().color());
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(8, 8), 1.6, 1.6);
        });
    }
    if (name == QLatin1String("search")) {
        return drawIcon(color, [](QPainter &p) {
            p.drawEllipse(QRectF(2.5, 2.5, 8, 8));
            p.drawLine(QPointF(9.3, 9.3), QPointF(13.5, 13.5));
        });
    }
    if (name == QLatin1String("find-in-files")) {
        return drawIcon(color, [](QPainter &p) {
            p.drawLine(QPointF(2, 3), QPointF(9, 3));
            p.drawLine(QPointF(2, 6.5), QPointF(6, 6.5));
            p.drawLine(QPointF(2, 10), QPointF(5, 10));
            p.drawEllipse(QRectF(7.5, 6.5, 5, 5));
            p.drawLine(QPointF(11.8, 11.8), QPointF(14, 14));
        });
    }
    if (name == QLatin1String("plus")) {
        return drawIcon(color, [](QPainter &p) {
            p.drawLine(QPointF(8, 3), QPointF(8, 13));
            p.drawLine(QPointF(3, 8), QPointF(13, 8));
        });
    }
    if (name == QLatin1String("minus"))
        return drawIcon(color, [](QPainter &p) { p.drawLine(QPointF(3, 8), QPointF(13, 8)); });
    if (name == QLatin1String("discard")) {
        // An arrow turning back.
        return drawIcon(color, [](QPainter &p) {
            QPainterPath path;
            path.moveTo(4, 6.5);
            path.lineTo(10, 6.5);
            path.cubicTo(13.5, 6.5, 13.5, 13, 10, 13);
            path.lineTo(6, 13);
            p.drawPath(path);
            p.drawPolyline(QPolygonF({QPointF(6.5, 4), QPointF(4, 6.5), QPointF(6.5, 9)}));
        });
    }
    if (name == QLatin1String("refresh")) {
        return drawIcon(color, [](QPainter &p) {
            p.drawArc(QRectF(3, 3, 10, 10), 30 * 16, 290 * 16);
            p.drawPolyline(QPolygonF({QPointF(13.2, 2.5), QPointF(13.2, 6), QPointF(9.7, 6)}));
        });
    }
    if (name == QLatin1String("branch")) {
        return drawIcon(color, [](QPainter &p) {
            p.drawEllipse(QRectF(3, 1.5, 3, 3));
            p.drawEllipse(QRectF(3, 11.5, 3, 3));
            p.drawEllipse(QRectF(10.5, 3.5, 3, 3));
            p.drawLine(QPointF(4.5, 4.5), QPointF(4.5, 11.5));
            QPainterPath path;
            path.moveTo(12, 6.5);
            path.cubicTo(12, 10, 4.5, 8, 4.5, 11.5);
            p.drawPath(path);
        });
    }
    if (name == QLatin1String("collapse-all")) {
        // Two chevrons pointing at each other: everything folds to the middle.
        return drawIcon(color, [](QPainter &p) {
            p.drawPolyline(QPolygonF({QPointF(4.5, 2.5), QPointF(8, 6), QPointF(11.5, 2.5)}));
            p.drawPolyline(QPolygonF({QPointF(4.5, 13.5), QPointF(8, 10), QPointF(11.5, 13.5)}));
        });
    }
    if (name == QLatin1String("more")) {
        // Three dots, one above the other.
        return drawIcon(color, [](QPainter &p) {
            p.setBrush(p.pen().color());
            p.setPen(Qt::NoPen);
            for (const qreal y : {3.5, 8.0, 12.5})
                p.drawEllipse(QPointF(8, y), 1.4, 1.4);
        });
    }
    if (name == QLatin1String("appearance")) {
        // A disc, half filled: light and dark.
        return drawIcon(color, [](QPainter &p) {
            p.drawEllipse(QRectF(2, 2, 12, 12));
            QPainterPath half;
            half.moveTo(8, 2);
            half.arcTo(QRectF(2, 2, 12, 12), 90, 180);
            half.closeSubpath();
            p.setBrush(p.pen().color());
            p.drawPath(half);
        });
    }
    if (name == QLatin1String("keyboard")) {
        return drawIcon(color, [](QPainter &p) {
            p.drawRoundedRect(QRectF(1, 3.5, 14, 9), 2, 2);
            p.setBrush(p.pen().color());
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 4; ++i) {
                p.drawEllipse(QPointF(4 + i * 2.7, 6.5), 0.8, 0.8);
                if (i == 0 || i == 3)
                    p.drawEllipse(QPointF(4 + i * 2.7, 9.5), 0.8, 0.8);
            }
            p.drawRoundedRect(QRectF(6, 8.8, 4, 1.4), 0.7, 0.7);
        });
    }
    if (name == QLatin1String("settings")) {
        // A gear: a ring and eight teeth.
        return drawIcon(color, [](QPainter &p) {
            p.drawEllipse(QPointF(8, 8), 2.2, 2.2);
            p.drawEllipse(QPointF(8, 8), 4.6, 4.6);
            for (int i = 0; i < 8; ++i) {
                const qreal angle = i * M_PI / 4;
                p.drawLine(QPointF(8 + 4.6 * std::cos(angle), 8 + 4.6 * std::sin(angle)),
                           QPointF(8 + 6.6 * std::cos(angle), 8 + 6.6 * std::sin(angle)));
            }
        });
    }
    // Command palette: a prompt.
    return drawIcon(color, [](QPainter &p) {
        p.drawRoundedRect(QRectF(1.5, 2.5, 13, 11), 2, 2);
        p.drawPolyline(QPolygonF({QPointF(4.5, 6), QPointF(6.5, 8), QPointF(4.5, 10)}));
        p.drawLine(QPointF(8, 10), QPointF(11, 10));
    });
}

} // namespace

} // namespace UiIcons
