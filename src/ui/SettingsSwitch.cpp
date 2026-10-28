// SPDX-License-Identifier: GPL-3.0-only

#include "SettingsSwitch.h"

#include "UiMetrics.h"

#include <QPainter>

SettingsSwitch::SettingsSwitch(QWidget *parent)
    : QCheckBox(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize SettingsSwitch::sizeHint() const
{
    return {UiMetrics::scaled(40), UiMetrics::scaled(26)};
}

QSize SettingsSwitch::minimumSizeHint() const
{
    return sizeHint();
}

bool SettingsSwitch::hitButton(const QPoint &point) const
{
    return rect().contains(point);
}

void SettingsSwitch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled())
        painter.setOpacity(0.45);
    const int trackWidth = UiMetrics::scaled(36);
    const int trackHeight = UiMetrics::scaled(20);
    const QRect track((width() - trackWidth) / 2, (height() - trackHeight) / 2, trackWidth, trackHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(isChecked() ? QPalette::Accent : QPalette::Mid));
    painter.drawRoundedRect(track, trackHeight / 2.0, trackHeight / 2.0);
    const int inset = UiMetrics::scaled(3);
    const int diameter = trackHeight - 2 * inset;
    const int knobX = isChecked() ? track.right() - diameter - inset + 1 : track.left() + inset;
    painter.setBrush(palette().color(QPalette::BrightText));
    painter.drawEllipse(QRect(knobX, track.top() + inset, diameter, diameter));
    if (hasFocus()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(palette().color(QPalette::Accent), 1, Qt::DotLine));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);
    }
}
