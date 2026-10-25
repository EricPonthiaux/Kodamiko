// SPDX-License-Identifier: GPL-3.0-only

#include "ModeTabBar.h"

#include "UiMetrics.h"

QSize ModeTabBar::tabSizeHint(int index) const
{
    QSize size = QTabBar::tabSizeHint(index);
    size.setHeight(UiMetrics::controlHeight());
    return size;
}
