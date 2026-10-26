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

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateMotion();

    QTimer *timer_;
    int penWidthPx_;
    int angle_ = 0;
    bool spinning_ = false;
};
