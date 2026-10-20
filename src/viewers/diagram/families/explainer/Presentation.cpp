// SPDX-License-Identifier: GPL-3.0-only

#include "Presentation.h"

#include "viewers/diagram/Board.h"

#include <QCoreApplication>
#include <QPainter>

#include <cmath>

namespace diagram::explainer::detail {

namespace {

QColor valid(const QColor &color, const QColor &fallback)
{
    return color.isValid() ? color : fallback;
}

} // namespace

QString tr(const char *text)
{
    return QCoreApplication::translate("ExplainerDiagram", text);
}

QColor toneFill(const Board &board, const QString &tone)
{
    const Style &style = board.style();
    const QColor fallback = style.shape.fill;
    if (tone == QLatin1String("accent"))
        return valid(style.tones.group, fallback);
    if (tone == QLatin1String("info"))
        return valid(style.tones.cool, fallback);
    if (tone == QLatin1String("success"))
        return valid(style.tones.data, valid(style.tones.sage, fallback));
    if (tone == QLatin1String("warning"))
        return valid(style.tones.warm, fallback);
    if (tone == QLatin1String("danger"))
        return valid(style.tones.rose, fallback);
    return valid(style.tones.sage, fallback);
}

QColor toneStroke(const Board &board, const QString &tone)
{
    const Style &style = board.style();
    if (tone == QLatin1String("accent"))
        return valid(style.tones.strong, style.link.color);
    return style.link.color;
}

QString oneOf(const QString &value, const QStringList &allowed, const QString &fallback)
{
    return allowed.contains(value) ? value : fallback;
}

QList<std::pair<QString, QString>> toneChoices()
{
    return {{QStringLiteral("neutral"), tr("Neutral")},  {QStringLiteral("accent"), tr("Accent")},
            {QStringLiteral("info"), tr("Information")}, {QStringLiteral("success"), tr("Success")},
            {QStringLiteral("warning"), tr("Warning")},  {QStringLiteral("danger"), tr("Danger")}};
}

QList<std::pair<QString, QString>> arrowChoices()
{
    return {{QStringLiteral("none"), tr("None")},
            {QStringLiteral("start"), tr("At the start")},
            {QStringLiteral("end"), tr("At the end")},
            {QStringLiteral("both"), tr("Both ends")}};
}

Property descriptionProperty()
{
    return {QStringLiteral("description"), tr("Description"), Property::Kind::Lines, {}};
}

void smallShadow(const Board &board, const std::function<void(const QColor &, QPointF)> &paint)
{
    QColor shadow = board.style().palette.shadow;
    const qreal alpha = shadow.alphaF();
    for (int i = 0; i < 2; ++i) {
        shadow.setAlphaF(float(alpha * (0.5 - i * 0.2)));
        paint(shadow, QPointF(0, 1.5 + i));
    }
}

QList<qreal> dashOf(const QString &line)
{
    if (line == QLatin1String("dashed"))
        return {8, 5};
    if (line == QLatin1String("dotted"))
        return {2, 5};
    return {};
}

void arrowHead(QPainter &painter, QPointF tip, QPointF previous, const QColor &color)
{
    fillArrowHead(painter, tip, std::atan2(tip.y() - previous.y(), tip.x() - previous.x()), strokeArrow, color);
}

} // namespace diagram::explainer::detail
