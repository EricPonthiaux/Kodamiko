// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ui/HoverProgress.h"

#include <QWidget>

// The line between the two sides of a DiffView side by side, which the
// mouse drags to give one side more room: a few pixels wide over the gap
// between them, with a splitter's cursor and grip (see SplitterGrip), the
// grip in the accent color under the mouse. Transparent around the grip:
// the gap shows through.
class SideBySideDivider : public QWidget {
    Q_OBJECT

public:
    explicit SideBySideDivider(QWidget *parent);

signals:
    // Dragged to `x`, in the parent's coordinates.
    void dragged(int x);

protected:
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    HoverProgress hover_{this};
};
