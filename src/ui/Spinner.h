// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QWidget>

class QTimer;

// A turning arc in the accent color, for a wait of unknown length. It stands
// still when motion is turned off (Motion::policy()).
class Spinner : public QWidget {
    Q_OBJECT

public:
    // 40 px across by default; smaller for a line of text (a status bar).
    explicit Spinner(QWidget *parent, int diameterPx = 40);

    // Shows it turning, or hides it.
    void setSpinning(bool spinning);

    // Adds a ring around a control's icon while the control is visible.
    // It follows the control's size and lets all pointer input through.
    static void surround(QWidget *control);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void updateMotion();

    QTimer *timer_;
    int angle_ = 0;
    bool spinning_ = false;
};
