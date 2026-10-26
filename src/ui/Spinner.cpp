// SPDX-License-Identifier: GPL-3.0-only

#include "Spinner.h"

#include "Motion.h"

#include <QPainter>
#include <QTimer>

#include <algorithm>

namespace {

constexpr int frameMs = 16;
constexpr int degreesPerFrame = 6;
constexpr int arcDegrees = 100;
// The pen's width, in 1/13 of the diameter: 3 px for the default 40 px.
constexpr int diameterPerPenWidth = 13;

} // namespace

Spinner::Spinner(QWidget *parent, int diameterPx)
    : QWidget(parent)
    , timer_(new QTimer(this))
    , penWidthPx_(std::max(2, diameterPx / diameterPerPenWidth))
{
    setFixedSize(diameterPx, diameterPx);
    timer_->setInterval(frameMs);
    connect(timer_, &QTimer::timeout, this, [this] {
        angle_ = (angle_ + degreesPerFrame) % 360;
        update();
    });
    connect(&Motion::policy(), &Motion::Policy::enabledChanged, this, [this] { updateMotion(); });
}

void Spinner::setSpinning(bool spinning)
{
    spinning_ = spinning;
    updateMotion();
    setVisible(spinning);
}

void Spinner::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal inset = penWidthPx_ + 1;
    const QRectF area = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
    QColor track = palette().color(QPalette::PlaceholderText);
    track.setAlphaF(0.25);
    painter.setPen(QPen(track, penWidthPx_));
    painter.drawEllipse(area);
    painter.setPen(QPen(palette().color(QPalette::Highlight), penWidthPx_, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(area, -angle_ * 16, arcDegrees * 16);
}

void Spinner::updateMotion()
{
    if (spinning_ && Motion::policy().isEnabled())
        timer_->start();
    else
        timer_->stop();
    update();
}
