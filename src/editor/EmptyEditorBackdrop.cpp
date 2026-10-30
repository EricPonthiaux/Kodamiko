// SPDX-License-Identifier: GPL-3.0-only

#include "EmptyEditorBackdrop.h"

#include "ui/BrandMark.h"

#include <QPainter>
#include <QRadialGradient>

#include <algorithm>
#include <utility>

namespace {

// As BrandHero's field: centred a little above the middle, as wide as most
// of the area, faint.
constexpr qreal tintCentreHeight = 0.45; // of the area's height
constexpr qreal tintRadius = 0.6;        // of the area's width
constexpr int tintAlpha = 22;

// The mark at the heart of the tint: a watermark, faded so that it reads as
// the background rather than as something to click, and no larger than the
// welcome's.
constexpr qreal markShare = 0.22; // of the area's smaller side
constexpr qreal largestMarkHeight = 144;
constexpr qreal markOpacity = 0.35;

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
    paintMark(painter, tint.center());
}

void EmptyEditorBackdrop::paintMark(QPainter &painter, QPointF centre) const
{
    const qreal side = std::min(largestMarkHeight, std::min(width(), height()) * markShare);
    const QRectF square(centre.x() - side / 2, centre.y() - side / 2, side, side);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(markOpacity);
    BrandMark::paint(&painter, BrandMark::centredArea(square, 1), BrandMark::colorsFor(theme_));
}
