// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QObject>

class QMouseEvent;
class QWidget;

// Frame Duet draws itself around its window when the system's is
// turned off: rounded corners, a soft shadow, a fine outline, and the edges
// to resize it (the system moves and resizes, so snapping still works).
// Maximized or full screen, the window fills the screen without them.
class WindowFrame : public QObject {
    Q_OBJECT

public:
    // Width of the shadow around the window, which also takes the resizing.
    static constexpr int shadowWidth = 12;
    static constexpr int cornerRadius = 10;

    explicit WindowFrame(QWidget *window);

    // Frameless: Duet's frame; otherwise the system's.
    void setFrameless(bool frameless);

    bool isFrameless() const { return frameless_; }

    void applyTheme(const Theme &theme);

    // Edges to resize from at `position` in a window of `size` whose
    // shadow is `margin` wide; none inside the window.
    static Qt::Edges edgesAt(const QPoint &position, const QSize &size, int margin);
    // Resize cursor for `edges`; the arrow for none.
    static Qt::CursorShape cursorFor(Qt::Edges edges);

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateMargins();
    void paint();
    bool drawsShadow() const;
    // The resize cursor over the shadow, while no button is held; the
    // children's own elsewhere.
    void updateCursor(const QMouseEvent *mouse);
    // A left press on the shadow resizes the window; returns whether it did.
    bool startResize(const QMouseEvent *mouse);

    QWidget *window_;
    Theme theme_;
    bool frameless_ = false;
};
