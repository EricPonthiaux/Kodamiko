// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "SketchScene.h"

#include <QColor>

class QPainter;

// How DiagramSketch draws the shapes and links of a SketchScene, blank: bars
// stand for their text.
namespace SketchPainting {

// The colors of a sketch: of the shapes and the pen, of the links, of the
// blank text, and of the card behind them.
struct Ink {
    QColor accent;
    QColor line;
    QColor text;
    QColor surface;
};

// `node` as it appears, from nothing (`progress` 0) to its full size (1).
void drawNode(QPainter &painter, const SketchNode &node, qreal progress, const Ink &ink);
// The first `progress` of `link` with the pen at its tip; its end and its
// label once fully drawn (1).
void drawLink(QPainter &painter, const SketchLink &link, qreal progress, const Ink &ink);

// Whether `shape` is drawn under the links: a frame or a lifeline that
// they cross.
bool isBackdrop(SketchShape shape);

} // namespace SketchPainting
