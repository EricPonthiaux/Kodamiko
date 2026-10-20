// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "viewers/diagram/Shape.h"

#include <QColor>
#include <QList>
#include <QPointF>
#include <QString>

#include <functional>
#include <utility>

class QPainter;

// What the explainer's elements share: their text, their tones, their
// property choices, and how what has no fill gets its shadow and its heads.
namespace diagram::explainer::detail {

inline const Font font(14);
constexpr qreal lineHeight = 20;
// The size of the heads a free board draws itself (strokes).
constexpr qreal strokeArrow = 11;

QString tr(const char *text);
// A tone ("neutral", "accent", "info", "success", "warning", "danger") on the
// board's style; an unknown tone is drawn as a neutral one.
QColor toneFill(const Board &board, const QString &tone);
QColor toneStroke(const Board &board, const QString &tone);
// `value` when it is one of `allowed`, else `fallback`.
QString oneOf(const QString &value, const QStringList &allowed, const QString &fallback);
QList<std::pair<QString, QString>> toneChoices();
QList<std::pair<QString, QString>> arrowChoices();
Property descriptionProperty();
// sidecar's withSmallShadow (blur 4, offset 2) for what has no fill to
// shadow: `paint` draws faint copies a little lower.
void smallShadow(const Board &board, const std::function<void(const QColor &, QPointF)> &paint);
// The dash pattern of a stroke's line style ("solid", "dashed", "dotted").
QList<qreal> dashOf(const QString &line);
// A filled head whose tip is `tip`, pointing away from `previous`.
void arrowHead(QPainter &painter, QPointF tip, QPointF previous, const QColor &color);

} // namespace diagram::explainer::detail
