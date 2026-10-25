// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QRectF>
#include <QWidget>

// Duet's mark at the start of the title bar, without the app icon's tile,
// drawn at the screen's resolution. Clicks go through: dragging it moves the
// window, like the bar. Its height is the bar's controls'; its width runs
// from its left edge to `trailingGap` pixels after the mark's shapes.
class BrandLogo : public QWidget {
public:
    // `theme` is the title bar's, which owns the logo and outlives it.
    BrandLogo(const Theme *theme, QWidget *parent);

    // Centers the mark in the first `pixels` of the logo (a column below
    // it), at least the mark's own square.
    void setPlaceWidth(int pixels);
    // The mark at `scale` (0 to 1) of its square.
    void setScale(qreal scale);
    // The room left after the mark's shapes, before the logo ends.
    void setTrailingGap(int pixels);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRectF markArea() const;

    const Theme *theme_;
    int placeWidth_ = 0;
    qreal scale_ = 1;
    int trailingGap_ = 0;
};
