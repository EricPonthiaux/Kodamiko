// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QTabBar>

// The tabs of the status bar's mode switcher: as high as the bars' other
// boxes (UiMetrics::controlHeight), so that they never stand taller than a
// tab and leave room above and below them in the bar.
class ModeTabBar : public QTabBar {
public:
    using QTabBar::QTabBar;

protected:
    QSize tabSizeHint(int index) const override;
};
