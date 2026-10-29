// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/Theme.h"

#include <QColor>
#include <QRectF>
#include <QString>

class QPainter;

// A project's avatar on the welcome window: its initials on a tint of a
// color of the theme's syntax, picked by its folder so that a project
// keeps it in every theme's palette.
namespace ProjectAvatar {

// "angular-shop" → "AS", "Kodamiko" → "KO".
QString initials(const QString &name);
// The color of the project at `path`, one of `theme`'s syntax colors.
QColor color(const Theme &theme, const QString &path);
// `text` centred on a rounded tint of `color`, in the painter's font.
void paint(QPainter *painter, const QRectF &area, const QString &text, const QColor &color);

} // namespace ProjectAvatar
