// SPDX-License-Identifier: GPL-3.0-only

#include "PulsingButton.h"

#include "Motion.h"

#include <QPainter>
#include <QVariantAnimation>

namespace {

constexpr int pulseMs = 1600;
constexpr qreal fillAlpha = 0.28;
constexpr qreal borderAlpha = 0.9;
constexpr qreal radiusPx = 4;

} // namespace

PulsingButton::PulsingButton(QWidget *parent)
    : QToolButton(parent)
    , pulse_(new QVariantAnimation(this))
{
    pulse_->setDuration(pulseMs);
    pulse_->setLoopCount(-1);
    pulse_->setEasingCurve(QEasingCurve::InOutSine);
    pulse_->setKeyValueAt(0.0, 0.15);
    pulse_->setKeyValueAt(0.5, 1.0);
    pulse_->setKeyValueAt(1.0, 0.15);
    connect(pulse_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        strength_ = value.toReal();
        update();
    });
    connect(&Motion::policy(), &Motion::Policy::enabledChanged, this, [this] { updateMotion(); });
}

void PulsingButton::setPulseColor(const QColor &color)
{
    color_ = color;
    update();
}

void PulsingButton::showEvent(QShowEvent *event)
{
    QToolButton::showEvent(event);
    updateMotion();
}

void PulsingButton::hideEvent(QHideEvent *event)
{
    QToolButton::hideEvent(event);
    updateMotion();
}

void PulsingButton::paintEvent(QPaintEvent *event)
{
    QToolButton::paintEvent(event);
    if (!color_.isValid())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor fill = color_;
    fill.setAlphaF(fillAlpha * strength_);
    QColor border = color_;
    border.setAlphaF(borderAlpha * strength_);
    painter.setPen(QPen(border, 1));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radiusPx, radiusPx);
}

void PulsingButton::updateMotion()
{
    if (isVisible() && Motion::policy().isEnabled()) {
        if (pulse_->state() != QAbstractAnimation::Running)
            pulse_->start();
        return;
    }
    pulse_->stop();
    strength_ = 1.0;
    update();
}
