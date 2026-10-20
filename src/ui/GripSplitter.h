// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QColor>
#include <QSplitter>

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

    explicit GripSplitter(Qt::Orientation orientation, QWidget *parent = nullptr);

    // For the handle before the widget at `index`, side by side.
    void setHandleRule(int index, const HandleRule &rule);

protected:
    QSplitterHandle *createHandle() override;
};
