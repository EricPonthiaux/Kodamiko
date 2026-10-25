// SPDX-License-Identifier: GPL-3.0-only

#include "AgentIndicator.h"

#include "AgentProvider.h"
#include "ui/Motion.h"
#include "ui/ThemeStyle.h"

#include <QCoreApplication>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>

#include <chrono>

namespace {

constexpr int paddingPx = 4;
constexpr int iconPx = 16;
constexpr qreal radiusPx = 4;

// The run block's tints (see ThemeStyle), here in the state's color.
constexpr qreal backgroundShare = 0.08;
constexpr qreal hoverShare = 0.18;
constexpr qreal borderShare = 0.3;

} // namespace

QColor agentStatusColor(AgentStatus status, const Theme &theme)
{
    switch (status) {
    case AgentStatus::Ready:
        return theme.mutedText;
    case AgentStatus::Working:
        return theme.warning;
    case AgentStatus::NeedsYou:
        return theme.accent;
    case AgentStatus::Done:
        // No theme has a color for success: the green of One Dark, and of
        // One Light on light themes.
        return {theme.isDark ? "#98c379" : "#50a14f"};
    }
    return theme.mutedText;
}

QString agentStatusText(AgentStatus status)
{
    switch (status) {
    case AgentStatus::Ready:
        return QCoreApplication::translate("AgentIndicator", "Ready");
    case AgentStatus::Working:
        return QCoreApplication::translate("AgentIndicator", "Working");
    case AgentStatus::NeedsYou:
        return QCoreApplication::translate("AgentIndicator", "Needs you");
    case AgentStatus::Done:
        return QCoreApplication::translate("AgentIndicator", "Done");
    }
    return {};
}

AgentIndicator::AgentIndicator(QWidget *parent)
    : QAbstractButton(parent)
    , theme_(Theme::dark())
{
    setFocusPolicy(Qt::NoFocus);
    updateAccessibleName();
    connect(&animationTimer_, &QTimer::timeout, this, &AgentIndicator::advanceFrame);
    connect(&Motion::policy(), &Motion::Policy::enabledChanged, this, &AgentIndicator::updateAnimation);
}

void AgentIndicator::setStatus(AgentStatus status)
{
    if (status == status_)
        return;
    status_ = status;
    updateAccessibleName();
    frame_ = 0;
    updateAnimation();
}

void AgentIndicator::setProvider(const AgentProvider *provider)
{
    provider_ = provider;
    frame_ = 0;
    updateAccessibleName();
    updateAnimation();
}

void AgentIndicator::updateAccessibleName()
{
    const QString agent = provider_ ? provider_->name() : QString();
    setAccessibleName(QStringLiteral("%1: %2").arg(agent, agentStatusText(status_)));
}

void AgentIndicator::updateAnimation()
{
    animationTimer_.stop();
    update();
    if (!provider_ || !Motion::policy().isEnabled())
        return;
    if (const std::optional<std::chrono::milliseconds> step = provider_->mark().frameStep(status_))
        animationTimer_.start(*step);
}

void AgentIndicator::applyTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

QSize AgentIndicator::sizeHint() const
{
    // As tall as the status bar's text, square whatever the state: the bar
    // keeps its layout as the state changes.
    // The contents margins set the room around it.
    const int side = std::max(iconPx, fontMetrics().height()) + 2 * paddingPx;
    return QSize(side, side).grownBy(contentsMargins());
}

bool AgentIndicator::hitButton(const QPoint &pos) const
{
    return contentsRect().contains(pos);
}

void AgentIndicator::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor color = agentStatusColor(status_, theme_);
    paintBackground(painter, color);
    const QRectF iconBox(QPointF(0, 0), QSizeF(iconPx, iconPx));
    if (provider_)
        provider_->mark().paint(painter, iconBox.translated(QRectF(contentsRect()).center() - iconBox.center()), color,
                                {.status = status_, .index = frame_});
}

void AgentIndicator::paintBackground(QPainter &painter, const QColor &color) const
{
    const qreal highlight = isDown() ? 1.0 : hover_.progress();
    const qreal share = backgroundShare + (hoverShare - backgroundShare) * highlight;
    QColor background = ThemeStyle::mixed(theme_.window, color, share);
    QColor border = ThemeStyle::mixed(theme_.window, color, borderShare);
    if (status_ == AgentStatus::Ready) {
        // At rest it stays discreet: the theme's own neutral colors.
        background = ThemeStyle::mixed(theme_.window, theme_.hover, highlight);
        border = theme_.line;
    }
    painter.setPen(QPen(border, 1));
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(contentsRect()).adjusted(0.5, 0.5, -0.5, -0.5), radiusPx, radiusPx);
}

void AgentIndicator::advanceFrame()
{
    const AgentMark &mark = provider_->mark();
    frame_ = (frame_ + 1) % mark.loopFrames();
    if (mark.isFinished({.status = status_, .index = frame_}))
        animationTimer_.stop();
    update();
}

void AgentIndicator::enterEvent(QEnterEvent *event)
{
    hover_.setHovered(true);
    QAbstractButton::enterEvent(event);
}

void AgentIndicator::leaveEvent(QEvent *event)
{
    hover_.setHovered(false);
    QAbstractButton::leaveEvent(event);
}

void AgentIndicator::changeEvent(QEvent *event)
{
    // Preferences ▸ Fonts: the size follows the text.
    if (event->type() == QEvent::FontChange)
        updateGeometry();
    QAbstractButton::changeEvent(event);
}
