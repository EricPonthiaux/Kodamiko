// SPDX-License-Identifier: GPL-3.0-only

#include "LetterBadges.h"

#include <QHash>
#include <QPainter>

namespace {

QIcon draw(QChar letter, const QColor &color)
{
    QIcon icon;
    for (const int size : {16, 32}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const qreal margin = size / 16.0;
        painter.drawRoundedRect(QRectF(margin, margin, size - 2 * margin, size - 2 * margin), size / 4.0, size / 4.0);
        QFont font = painter.font();
        font.setPixelSize(size * 11 / 16);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, QString(letter));
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace

namespace LetterBadges {

QIcon icon(QChar letter, const QColor &color)
{
    static QHash<QString, QIcon> cache;
    const QString key = letter + QString::number(color.rgba(), 16);
    auto it = cache.constFind(key);
    if (it == cache.constEnd())
        it = cache.insert(key, draw(letter, color));
    return it.value();
}

} // namespace LetterBadges
