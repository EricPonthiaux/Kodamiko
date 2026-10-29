// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectAvatar.h"

#include <QPainter>
#include <QRegularExpression>

#include <array>

namespace {

constexpr qreal radiusRatio = 0.24; // of the avatar's height
constexpr int tintAlpha = 40;       // the background, 16% of the color

} // namespace

namespace ProjectAvatar {

QString initials(const QString &name)
{
    static const QRegularExpression separators(QStringLiteral("[-_.\\s]+"));
    const QStringList words = name.split(separators, Qt::SkipEmptyParts);
    if (words.size() >= 2)
        return (words[0].left(1) + words[1].left(1)).toUpper();
    return name.left(2).toUpper();
}

QColor color(const Theme &theme, const QString &path)
{
    static constexpr std::array<QStringView, 7> captures = {
        u"function", u"keyword", u"type", u"number", u"string", u"property", u"variable.parameter"};
    const int index = theme.syntaxStyleIndex(captures[qHash(path, 0) % captures.size()]);
    return index < 0 ? theme.accent : theme.syntaxStyles[index].second.foreground;
}

void paint(QPainter *painter, const QRectF &area, const QString &text, const QColor &color)
{
    QColor tint = color;
    tint.setAlpha(tintAlpha);
    const qreal radius = area.height() * radiusRatio;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(tint);
    painter->drawRoundedRect(area, radius, radius);
    painter->setPen(color);
    painter->drawText(area, Qt::AlignCenter, text);
    painter->restore();
}

} // namespace ProjectAvatar
