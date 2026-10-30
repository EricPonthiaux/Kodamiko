// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QRectF>

class QPainter;

// Kodamiko's mark: a geometric K, built from an editor caret and two diagonal
// strokes with precise negative-space cuts. Drawn rather than loaded, so it
// is sharp at every size; packaging/brand holds the same shapes as SVG.
namespace BrandMark {

// The colors of the mark's two shapes.
struct Colors {
    QColor caret;
    QColor block;
};

// The monogram, fitted in the square `area`.
void paint(QPainter *painter, const QRectF &area, const Colors &colors);
// The part of the square `area` the two shapes cover: the mark's visible
// edges, inside the blank margin of its grid.
QRectF inkRect(const QRectF &area);
// The mark's colors on the interface of `theme`: the caret in its text color,
// the diagonal strokes in its accent, so that the mark follows the theme.
Colors colorsFor(const Theme &theme);
// The mark alone, `size` pixels square, in the colors of `theme`: no tile.
QPixmap markPixmap(int size, const Theme &theme, qreal devicePixelRatio = 1);
// The square to paint the mark in so that its shapes are centred in
// `square` and fill `inkShare` (0 to 1) of its height.
QRectF centredArea(const QRectF &square, qreal inkShare);
// The app icon in the square `area`, in the colors of `theme`: the mark on a
// rounded tile of the theme's window color. The bundle's icon file
// (packaging/brand) keeps the default colors: systems show it while Kodamiko is
// not running.
void paintAppIcon(QPainter *painter, const QRectF &area, const Theme &theme);
// The app icon, `size` pixels square.
QPixmap appIcon(int size, const Theme &theme, qreal devicePixelRatio = 1);
// The app icon at the sizes systems ask for, for the windows, the Dock and
// the taskbar.
QIcon windowIcon(const Theme &theme);

} // namespace BrandMark
