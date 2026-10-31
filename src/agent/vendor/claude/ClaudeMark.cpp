// SPDX-License-Identifier: GPL-3.0-only

#include "ClaudeMark.h"

#include "ui/Motion.h"

#include <QPainter>

#include <array>
#include <cmath>

namespace {

constexpr int dotCount = 8;
constexpr auto sparkStep = std::chrono::milliseconds(110);
constexpr auto pulseStep = std::chrono::milliseconds(50);
constexpr int pulseFrames = 24; // a breath every 1.2 s
constexpr auto twinkleStep = std::chrono::milliseconds(100);
constexpr int twinkleFrames = 6; // three flashes of each set of dots

// The ring's dots by distance behind the brightest one: a trail that
// turns around the ring.
constexpr std::array<qreal, 4> trailOpacity{1.0, 0.7, 0.5, 0.32};
constexpr qreal restingOpacity = 0.25;
// The faintest the ring gets while it breathes.
constexpr qreal pulseLowOpacity = 0.3;

qreal dotOpacity(int dot, AgentMarkFrame markFrame)
{
    if (!Motion::policy().isEnabled())
        return 1;
    const int frame = markFrame.index;
    switch (markFrame.status) {
    case AgentStatus::Working: {
        const int litDot = frame % dotCount;
        const int behind = (litDot - dot + dotCount) % dotCount;
        return behind < int(trailOpacity.size()) ? trailOpacity[behind] : restingOpacity;
    }
    case AgentStatus::NeedsYou: {
        const qreal breath = 0.5 + 0.5 * std::cos(2 * M_PI * frame / pulseFrames);
        return pulseLowOpacity + (1 - pulseLowOpacity) * breath;
    }
    case AgentStatus::Done: {
        if (frame >= twinkleFrames)
            return 1;
        const bool isFlashing = (dot + frame) % 2 == 0;
        return isFlashing ? 1 : restingOpacity;
    }
    case AgentStatus::Ready:
        return 1;
    }
    return 1;
}

} // namespace

std::optional<std::chrono::milliseconds> ClaudeMark::frameStep(AgentStatus status) const
{
    switch (status) {
    case AgentStatus::Working:
        return sparkStep;
    case AgentStatus::NeedsYou:
        return pulseStep;
    case AgentStatus::Done:
        return twinkleStep;
    case AgentStatus::Ready:
        return std::nullopt;
    }
    return std::nullopt;
}

// The spark and the breath loop on a multiple of their periods.
int ClaudeMark::loopFrames() const
{
    return dotCount * pulseFrames;
}

// The twinkle plays once.
bool ClaudeMark::isFinished(AgentMarkFrame frame) const
{
    return frame.status == AgentStatus::Done && frame.index >= twinkleFrames;
}

void ClaudeMark::paint(QPainter &painter, const QRectF &box, const QColor &color, AgentMarkFrame frame) const
{
    const QPointF center = box.center();
    const qreal orbit = 5;
    painter.setPen(Qt::NoPen);
    for (int dot = 0; dot < dotCount; ++dot) {
        QColor dotColor = color;
        dotColor.setAlphaF(float(dotOpacity(dot, frame)));
        painter.setBrush(dotColor);
        const qreal angle = dot * M_PI / 4;
        const qreal radius = dot % 2 ? 1.0 : 1.4;
        const QPointF direction(std::cos(angle), std::sin(angle));
        painter.drawEllipse(center + orbit * direction, radius, radius);
    }
}
