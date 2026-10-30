// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QTabBar>

// The status bar's mode switcher: inset segments on a shared rounded track,
// as high as the bars' other boxes (UiMetrics::controlHeight).
class ModeTabBar : public QTabBar {
public:
    using QTabBar::QTabBar;

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize tabSizeHint(int index) const override;
    void tabLayoutChange() override;

private:
    QSize laidOutSizeHint_; // the size its layout last heard of
};
