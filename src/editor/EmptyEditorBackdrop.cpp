// SPDX-License-Identifier: GPL-3.0-only

#include "EmptyEditorBackdrop.h"

#include <QPainter>
#include <QRadialGradient>

#include <utility>

namespace {

// As BrandHero's field: centred a little above the middle, as wide as most
// of the area, faint.
constexpr qreal tintCentreHeight = 0.45; // of the area's height
constexpr qreal tintRadius = 0.6;        // of the area's width
constexpr int tintAlpha = 22;

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

} // namespace

EmptyEditorBackdrop::EmptyEditorBackdrop(Theme theme, QWidget *parent)
    : QWidget(parent)
    , theme_(std::move(theme))
{
}

void EmptyEditorBackdrop::applyTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

void EmptyEditorBackdrop::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    const QRectF area = rect();
    QRadialGradient tint(QPointF(area.center().x(), area.height() * tintCentreHeight), area.width() * tintRadius);
    tint.setColorAt(0, withAlpha(theme_.accent, tintAlpha));
    tint.setColorAt(1, withAlpha(theme_.accent, 0));
    painter.fillRect(area, tint);
}
