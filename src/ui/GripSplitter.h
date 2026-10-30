// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QColor>
#include <QSplitter>

class QPainter;

// Splitter whose handles show where they are: a thin line along the whole
// handle, and a small grip in its middle to take hold of, in the accent
// color under the mouse.
class GripSplitter : public QSplitter {
    Q_OBJECT

public:
    // A horizontal rule of the panes beside a handle (a bar's bottom
    // border), carried across the handle from `sides` to its line: the rule
    // and the handle's line meet, and two rules make one.
    struct HandleRule {
        int y = -1; // from the handle's top; -1: no rule
        Qt::Edges sides;
        QColor color;
    };

    // The panes' backgrounds carried into a handle up to its line, each
    // half the color of the pane beside it: above `split`, a header's, with
    // its bottom border; below, the content's. Invalid colors leave the
    // handle's own background.
    struct HandleSides {
        int split = 0; // from the handle's top
        QColor leftHeader;
        QColor rightHeader;
        QColor leftBorder; // the headers' bottom borders, `borderWidth` high
        QColor rightBorder;
        int borderWidth = 0;
        QColor leftContent;
        QColor rightContent;
    };

    explicit GripSplitter(Qt::Orientation orientation, QWidget *parent = nullptr);

    // For the handle before the widget at `index`, side by side.
    void setHandleRule(int index, const HandleRule &rule);
    void setHandleSides(int index, const HandleSides &sides);

protected:
    QSplitterHandle *createHandle() override;
};

namespace SplitterGrip {

// The grip in the middle of `handle`: a small rounded bar along it (upright
// between panes side by side), in its palette's colors, `highlight` (0 at
// rest, 1 under the mouse) of the way to the accent color.
void paint(QPainter &painter, const QWidget &handle, Qt::Orientation orientation, qreal highlight);

} // namespace SplitterGrip
