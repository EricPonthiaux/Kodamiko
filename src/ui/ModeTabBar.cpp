// SPDX-License-Identifier: GPL-3.0-only

#include "ModeTabBar.h"

#include "ThemeStyle.h"
#include "UiMetrics.h"

#include <QPainter>

void ModeTabBar::paintEvent(QPaintEvent *event)
{
    // Paint the shared track independently of the tab shapes, so hiding a
    // mode or collapsing its label keeps the same continuous frame.
    const Theme &theme = ThemeStyle::applied();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(theme.line);
    painter.setBrush(ThemeStyle::mixed(theme.window, theme.background, 0.35));
    const QRectF track = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    constexpr qreal radius = 7;
    painter.drawRoundedRect(track, radius, radius);
    painter.end();
    QTabBar::paintEvent(event);
}

// QTabBar does not tell its layout when a tab is hidden or shown
// (setTabVisible): the bar would keep a hidden mode's room, its frame
// running empty past the last tab. Only a new size is told, which keeps the
// layout from looping.
void ModeTabBar::tabLayoutChange()
{
    QTabBar::tabLayoutChange();
    const QSize hint = sizeHint();
    if (hint == laidOutSizeHint_)
        return;
    laidOutSizeHint_ = hint;
    updateGeometry();
}

QSize ModeTabBar::tabSizeHint(int index) const
{
    QSize size = QTabBar::tabSizeHint(index);
    size.setHeight(UiMetrics::controlHeight());
    return size;
}
