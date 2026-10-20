// SPDX-License-Identifier: GPL-3.0-only

#include "HoverProgress.h"

#include "Motion.h"

#include <QWidget>

#include <algorithm>
#include <cmath>

HoverProgress::HoverProgress(QWidget *widget)
{
    animation_.setEasingCurve(Motion::standardCurve());
    QObject::connect(&animation_, &QVariantAnimation::valueChanged, widget, [this, widget](const QVariant &value) {
        progress_ = value.toReal();
        widget->update();
    });
    Motion::track(&animation_);
}

void HoverProgress::setHovered(bool hovered)
{
    if (hovered == isHovered_)
        return;
    isHovered_ = hovered;
    const qreal target = hovered ? 1.0 : 0.0;
    animation_.stop();
    // Reversed halfway, it takes half the time back.
    const qreal distance = std::abs(target - progress_);
    const int duration = std::max(1, int(std::lround(qreal(Motion::hoverDuration.count()) * distance)));
    animation_.setDuration(Motion::policy().isEnabled() ? duration : 0);
    animation_.setStartValue(progress_);
    animation_.setEndValue(target);
    animation_.start();
}
