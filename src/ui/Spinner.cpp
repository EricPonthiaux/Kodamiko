// SPDX-License-Identifier: GPL-3.0-only

#include "Spinner.h"

#include "Motion.h"

#include <QEvent>
#include <QPainter>
#include <QTimer>

#include <algorithm>

namespace {

constexpr int frameMs = 16;
constexpr int degreesPerFrame = 6;
constexpr int arcDegrees = 100;
// The pen's width, in 1/13 of the diameter: 3 px for the default 40 px.
constexpr int diameterPerPenWidth = 13;

class SpinnerOverlay : public Spinner {
public:
    explicit SpinnerOverlay(QWidget *control)
        : Spinner(control)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        control->installEventFilter(this);
        fitControl();
        setSpinning(true);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize)
            fitControl();
        return Spinner::eventFilter(watched, event);
    }

private:
    void fitControl()
    {
        const QWidget *control = parentWidget();
        const int diameter = std::min(control->width(), control->height());
        setFixedSize(diameter, diameter);
        move((control->width() - diameter) / 2, (control->height() - diameter) / 2);
    }
};

} // namespace

Spinner::Spinner(QWidget *parent, int diameterPx)
    : QWidget(parent)
    , timer_(new QTimer(this))
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
    setVisible(spinning);
    updateMotion();
}

void Spinner::surround(QWidget *control)
{
    new SpinnerOverlay(control);
}

void Spinner::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateMotion();
}

void Spinner::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    updateMotion();
}

void Spinner::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int penWidth = std::max(2, width() / diameterPerPenWidth);
    const qreal inset = penWidth + 1;
    const QRectF area = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
    QColor track = palette().color(QPalette::PlaceholderText);
    track.setAlphaF(0.25);
    painter.setPen(QPen(track, penWidth));
    painter.drawEllipse(area);
    painter.setPen(QPen(palette().color(QPalette::Highlight), penWidth, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(area, -angle_ * 16, arcDegrees * 16);
}

void Spinner::updateMotion()
{
    if (spinning_ && isVisible() && Motion::policy().isEnabled())
        timer_->start();
    else
        timer_->stop();
    update();
}
