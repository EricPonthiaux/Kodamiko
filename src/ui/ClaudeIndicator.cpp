#include "ClaudeIndicator.h"

#include "ThemeStyle.h"

#include <QCoreApplication>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>

#include <array>
#include <chrono>
#include <cmath>

namespace {

constexpr int paddingPx = 4;
constexpr int iconPx = 16;
constexpr qreal radiusPx = 4;
constexpr int branchCount = 8;
constexpr auto sparkStep = std::chrono::milliseconds(110);
constexpr auto pulseStep = std::chrono::milliseconds(50);
constexpr int pulseFrames = 24; // a breath every 1.2 s
constexpr auto twinkleStep = std::chrono::milliseconds(100);
constexpr int twinkleFrames = 6; // three flashes of each set of branches

// The star's branches by distance behind the brightest one: a trail that
// turns around the star.
constexpr std::array<qreal, 4> trailOpacity{1.0, 0.7, 0.5, 0.32};
constexpr qreal restingOpacity = 0.25;
// The faintest the star gets while it breathes.
constexpr qreal pulseLowOpacity = 0.3;

// The run block's tints (see ThemeStyle), here in the state's color.
constexpr qreal backgroundShare = 0.08;
constexpr qreal hoverShare = 0.18;
constexpr qreal borderShare = 0.3;

} // namespace

QColor claudeStatusColor(ClaudeIndicator::Status status, const Theme &theme)
{
    switch (status) {
    case ClaudeIndicator::Status::Ready:
        return theme.mutedText;
    case ClaudeIndicator::Status::Working:
        return theme.warning;
    case ClaudeIndicator::Status::NeedsYou:
        return theme.accent;
    case ClaudeIndicator::Status::Done:
        // No theme has a color for success: the green of One Dark, and of
        // One Light on light themes.
        return {theme.isDark ? "#98c379" : "#50a14f"};
    }
    return theme.mutedText;
}

QString claudeStatusText(ClaudeIndicator::Status status)
{
    switch (status) {
    case ClaudeIndicator::Status::Ready:
        return QCoreApplication::translate("ClaudeIndicator", "Ready");
    case ClaudeIndicator::Status::Working:
        return QCoreApplication::translate("ClaudeIndicator", "Working");
    case ClaudeIndicator::Status::NeedsYou:
        return QCoreApplication::translate("ClaudeIndicator", "Needs you");
    case ClaudeIndicator::Status::Done:
        return QCoreApplication::translate("ClaudeIndicator", "Done");
    }
    return {};
}

ClaudeIndicator::ClaudeIndicator(QWidget *parent)
    : QAbstractButton(parent)
    , theme_(Theme::dark())
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(QStringLiteral("Claude: %1").arg(claudeStatusText(status_)));
    connect(&animationTimer_, &QTimer::timeout, this, &ClaudeIndicator::advanceFrame);
}

void ClaudeIndicator::setStatus(Status status)
{
    if (status == status_)
        return;
    status_ = status;
    setAccessibleName(QStringLiteral("Claude: %1").arg(claudeStatusText(status)));
    frame_ = 0;
    switch (status) {
    case Status::Working:
        animationTimer_.start(sparkStep);
        break;
    case Status::NeedsYou:
        animationTimer_.start(pulseStep);
        break;
    case Status::Done:
        animationTimer_.start(twinkleStep);
        break;
    case Status::Ready:
        animationTimer_.stop();
        break;
    }
    update();
}

void ClaudeIndicator::applyTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

QSize ClaudeIndicator::sizeHint() const
{
    // As tall as the title bar's text, square whatever the state: the bar
    // keeps its layout as the state changes.
    const int side = std::max(iconPx, fontMetrics().height()) + 2 * paddingPx;
    return {side, side};
}

void ClaudeIndicator::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor color = claudeStatusColor(status_, theme_);
    paintBackground(painter, color);
    const QRectF iconBox(QPointF(0, 0), QSizeF(iconPx, iconPx));
    paintStar(painter, iconBox.translated(QRectF(rect()).center() - iconBox.center()), color);
}

void ClaudeIndicator::paintBackground(QPainter &painter, const QColor &color) const
{
    const bool highlighted = hovered_ || isDown();
    QColor background = ThemeStyle::mixed(theme_.window, color, highlighted ? hoverShare : backgroundShare);
    QColor border = ThemeStyle::mixed(theme_.window, color, borderShare);
    if (status_ == Status::Ready) {
        // At rest it stays discreet: the theme's own neutral colors.
        background = highlighted ? theme_.hover : theme_.window;
        border = theme_.line;
    }
    painter.setPen(QPen(border, 1));
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radiusPx, radiusPx);
}

// Claude's burst (see UiIcons' "claude"): eight branches, the long ones
// on the axes.
void ClaudeIndicator::paintStar(QPainter &painter, const QRectF &box, const QColor &color) const
{
    const QPointF center = box.center();
    const qreal inner = 1.8;
    for (int branch = 0; branch < branchCount; ++branch) {
        QColor branchColor = color;
        branchColor.setAlphaF(float(branchOpacity(branch)));
        painter.setPen(QPen(branchColor, 1.5, Qt::SolidLine, Qt::RoundCap));
        const qreal angle = branch * M_PI / 4;
        const qreal outer = branch % 2 ? 5.2 : 6.3;
        const QPointF direction(std::cos(angle), std::sin(angle));
        painter.drawLine(center + inner * direction, center + outer * direction);
    }
}

// While Claude works, each branch lights up in turn; while it waits for the
// user, the whole star breathes; when a response arrives, the long and the
// short branches flash in turn, then the star shines still.
qreal ClaudeIndicator::branchOpacity(int branch) const
{
    switch (status_) {
    case Status::Working: {
        const int litBranch = frame_ % branchCount;
        const int behind = (litBranch - branch + branchCount) % branchCount;
        return behind < int(trailOpacity.size()) ? trailOpacity[behind] : restingOpacity;
    }
    case Status::NeedsYou: {
        const qreal breath = 0.5 + 0.5 * std::cos(2 * M_PI * frame_ / pulseFrames);
        return pulseLowOpacity + (1 - pulseLowOpacity) * breath;
    }
    case Status::Done: {
        if (frame_ >= twinkleFrames)
            return 1;
        const bool isFlashing = (branch + frame_) % 2 == 0;
        return isFlashing ? 1 : restingOpacity;
    }
    case Status::Ready:
        return 1;
    }
    return 1;
}

void ClaudeIndicator::advanceFrame()
{
    // The spark and the breath loop on a multiple of their periods; the
    // twinkle plays once.
    frame_ = (frame_ + 1) % (branchCount * pulseFrames);
    if (status_ == Status::Done && frame_ >= twinkleFrames)
        animationTimer_.stop();
    update();
}

void ClaudeIndicator::enterEvent(QEnterEvent *event)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ClaudeIndicator::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ClaudeIndicator::changeEvent(QEvent *event)
{
    // Preferences ▸ Fonts: the size follows the text.
    if (event->type() == QEvent::FontChange)
        updateGeometry();
    QAbstractButton::changeEvent(event);
}
