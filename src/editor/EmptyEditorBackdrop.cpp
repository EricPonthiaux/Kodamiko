// SPDX-License-Identifier: GPL-3.0-only

#include "EmptyEditorBackdrop.h"

#include "ui/BrandMark.h"

#include <QPainter>

#include <algorithm>
#include <utility>

namespace {

// A watermark, faded so that it reads as the background rather than as
// something to click, and no larger than the welcome's mark.
constexpr qreal markShare = 0.22; // of the area's smaller side
constexpr qreal largestMarkHeight = 144;
constexpr qreal markOpacity = 0.35;

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
    const qreal side = std::min(largestMarkHeight, std::min(width(), height()) * markShare);
    QRectF square(0, 0, side, side);
    square.moveCenter(QRectF(rect()).center());
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(markOpacity);
    BrandMark::paint(&painter, BrandMark::centredArea(square, 1), BrandMark::colorsFor(theme_));
}
