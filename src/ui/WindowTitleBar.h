// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Theme.h"

#include <QWidget>

class QLabel;
class WindowControls;

// The title bar of Duet's other frameless windows, matching the main one:
// the app icon, the window's title, and the window controls. Dragging it
// moves the window, a double-click maximizes it.
class WindowTitleBar : public QWidget {
    Q_OBJECT

public:
    // At the top of `parent`'s window, whose title it shows.
    explicit WindowTitleBar(QWidget *parent);

    void applyTheme(const Theme &theme);

    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updateIcon();

    Theme theme_;
    QLabel *icon_;
    QLabel *title_;
    WindowControls *controls_;
};
