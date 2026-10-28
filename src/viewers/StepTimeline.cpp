// SPDX-License-Identifier: GPL-3.0-only

#include "StepTimeline.h"

#include "ui/Motion.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <cmath>
#include <numbers>

namespace {

constexpr int shownSteps = 5;
constexpr int frameMs = 33;
constexpr qreal pulseMs = 1400;
constexpr qreal markerRadiusPx = 5;
constexpr qreal haloRadiusPx = 11;
constexpr int railWidthPx = 26;
// A row is this many times the font's height.
constexpr qreal rowSpacing = 1.75;

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(alpha);
    return color;
}

void drawCheck(QPainter &painter, QPointF center, const QColor &color)
{
    QPainterPath check(center + QPointF(-2.6, 0.2));
    check.lineTo(center + QPointF(-0.7, 2.1));
    check.lineTo(center + QPointF(2.8, -1.8));
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(check);
}

void drawCross(QPainter &painter, QPointF center, const QColor &color)
{
    constexpr qreal arm = 2.4;
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(center + QPointF(-arm, -arm), center + QPointF(arm, arm));
    painter.drawLine(center + QPointF(-arm, arm), center + QPointF(arm, -arm));
}

QColor errorColor(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128 ? QColor(0xe0, 0x6c, 0x75) : QColor(0xe4, 0x56, 0x49);
}

} // namespace

StepTimeline::StepTimeline(QWidget *parent)
    : QWidget(parent)
    , frame_(new QTimer(this))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    frame_->setInterval(frameMs);
    connect(frame_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    connect(&Motion::policy(), &Motion::Policy::enabledChanged, this, [this] { updateMotion(); });
    clock_.start();
}

void StepTimeline::clear()
{
    steps_.clear();
    setState(State::Running);
}

void StepTimeline::addSteps(const QStringList &steps)
{
    steps_ += steps;
    update();
}

void StepTimeline::setState(State state)
{
    state_ = state;
    updateMotion();
}

QSize StepTimeline::sizeHint() const
{
    return {railWidthPx * 10, rowHeight() * shownSteps};
}

QSize StepTimeline::minimumSizeHint() const
{
    return {railWidthPx * 4, rowHeight() * shownSteps};
}

void StepTimeline::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QStringList shown = steps_.mid(std::max<qsizetype>(0, steps_.size() - shownSteps));
    const int height = rowHeight();
    const qreal railX = railWidthPx / 2.0;
    const QColor muted = palette().color(QPalette::PlaceholderText);

    // The rail joins the markers; it fades out above the oldest step shown.
    if (shown.size() > 1) {
        QLinearGradient rail(0, 0, 0, height * (shown.size() - 1));
        rail.setColorAt(0, withAlpha(muted, steps_.size() > shownSteps ? 0 : 0.35));
        rail.setColorAt(0.4, withAlpha(muted, 0.35));
        painter.setPen(QPen(QBrush(rail), 1));
        painter.drawLine(QPointF(railX, height / 2.0), QPointF(railX, height * (shown.size() - 0.5)));
    }

    for (qsizetype i = 0; i < shown.size(); ++i) {
        const bool isCurrent = i + 1 == shown.size();
        const QRect row(0, int(i) * height, width(), height);
        drawMarker(painter, QPointF(railX, row.center().y() + 0.5), isCurrent);

        QFont font = this->font();
        font.setWeight(isCurrent ? QFont::DemiBold : QFont::Normal);
        painter.setFont(font);
        painter.setPen(isCurrent ? palette().color(QPalette::Text) : muted);
        const QRect textRect = row.adjusted(railWidthPx + 6, 0, 0, 0);
        const QString text = isCurrent && state_ == State::Running ? shown.at(i) + QStringLiteral("…") : shown.at(i);
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(font).elidedText(text, Qt::ElideRight, textRect.width()));
    }
}

void StepTimeline::drawMarker(QPainter &painter, QPointF center, bool isCurrent) const
{
    const QColor background = palette().color(QPalette::Base);
    const QColor muted = palette().color(QPalette::PlaceholderText);
    painter.setPen(Qt::NoPen);
    if (!isCurrent) {
        painter.setBrush(background);
        painter.drawEllipse(center, markerRadiusPx + 2, markerRadiusPx + 2);
        painter.setBrush(withAlpha(muted, 0.2));
        painter.drawEllipse(center, markerRadiusPx + 1, markerRadiusPx + 1);
        drawCheck(painter, center, muted);
        return;
    }
    if (state_ == State::Failed) {
        const QColor error = errorColor(palette());
        painter.setBrush(withAlpha(error, 0.18));
        painter.drawEllipse(center, markerRadiusPx + 3, markerRadiusPx + 3);
        drawCross(painter, center, error);
        return;
    }
    const QColor accent = palette().color(QPalette::Highlight);
    if (Motion::policy().isEnabled()) {
        // A halo that grows and fades, once per pulse.
        const qreal phase = std::fmod(qreal(clock_.elapsed()), pulseMs) / pulseMs;
        painter.setBrush(withAlpha(accent, 0.35 * (1 - phase)));
        const qreal radius = markerRadiusPx + (haloRadiusPx - markerRadiusPx) * std::sin(phase * std::numbers::pi / 2);
        painter.drawEllipse(center, radius, radius);
    }
    painter.setBrush(background);
    painter.drawEllipse(center, markerRadiusPx + 1, markerRadiusPx + 1);
    painter.setPen(QPen(accent, 2));
    painter.setBrush(withAlpha(accent, 0.25));
    painter.drawEllipse(center, markerRadiusPx - 1, markerRadiusPx - 1);
}

void StepTimeline::updateMotion()
{
    if (state_ == State::Running && Motion::policy().isEnabled())
        frame_->start();
    else
        frame_->stop();
    update();
}

int StepTimeline::rowHeight() const
{
    return int(std::lround(fontMetrics().height() * rowSpacing));
}
