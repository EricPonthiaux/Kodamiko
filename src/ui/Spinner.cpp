// SPDX-License-Identifier: GPL-3.0-only

#include "Spinner.h"

#include "Motion.h"

#include <QPainter>
#include <QTimer>

namespace {

constexpr int sizePx = 40;
constexpr int frameMs = 16;
constexpr int degreesPerFrame = 6;
constexpr int arcDegrees = 100;
constexpr int penWidthPx = 3;

} // namespace

Spinner::Spinner(QWidget *parent)
    : QWidget(parent)
    , timer_(new QTimer(this))
{
    setFixedSize(sizePx, sizePx);
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
    const QRectF area = QRectF(rect()).adjusted(4, 4, -4, -4);
    QColor track = palette().color(QPalette::PlaceholderText);
    track.setAlphaF(0.25);
    painter.setPen(QPen(track, penWidthPx));
    painter.drawEllipse(area);
    painter.setPen(QPen(palette().color(QPalette::Highlight), penWidthPx, Qt::SolidLine, Qt::RoundCap));
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
