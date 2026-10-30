// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QColor>
#include <QToolButton>

class QVariantAnimation;

// A tool button that calls for attention while it is shown: a halo of
// setPulseColor() breathes around it. Without motion (Motion::policy), the
// halo stays lit, still.
class PulsingButton : public QToolButton {
    Q_OBJECT

public:
    explicit PulsingButton(QWidget *parent);

    void setPulseColor(const QColor &color);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updateMotion();

    QVariantAnimation *pulse_;
    QColor color_;
    qreal strength_ = 1.0; // 0: no halo, 1: the halo at its brightest
};
