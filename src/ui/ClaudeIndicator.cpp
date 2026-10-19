#include "ClaudeIndicator.h"

#include "ThemeStyle.h"

#include <QCoreApplication>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>

#include <array>
#include <cmath>

namespace {

constexpr int paddingPx = 8;
constexpr int verticalPaddingPx = 4;
constexpr int iconPx = 16;
constexpr int iconGapPx = 6;
// On each side of the divider between "Claude" and the state.
constexpr int dividerGapPx = 9;
constexpr qreal radiusPx = 4;
constexpr int branchCount = 8;
constexpr int sparkStepMs = 110;

// The star's branches by distance behind the brightest one: a trail that
// turns around the star.
constexpr std::array<qreal, 4> trailOpacity{1.0, 0.7, 0.5, 0.32};
constexpr qreal restingOpacity = 0.25;

// The run block's tints (see ThemeStyle), here in the state's color.
constexpr qreal backgroundShare = 0.08;
constexpr qreal hoverShare = 0.18;
constexpr qreal borderShare = 0.3;

constexpr std::array allStatuses{ClaudeIndicator::Status::Ready, ClaudeIndicator::Status::Working,
                                 ClaudeIndicator::Status::NeedsYou, ClaudeIndicator::Status::Done};

qreal branchOpacity(int branch, int litBranch)
{
    const int behind = (litBranch - branch + branchCount) % branchCount;
    return behind < int(trailOpacity.size()) ? trailOpacity[behind] : restingOpacity;
}

// A speech bubble with three dots: Claude is waiting for an answer.
void paintBubble(QPainter &painter, const QRectF &box, const QColor &color)
{
    painter.save();
    painter.translate(box.topLeft());
    painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath bubble;
    bubble.addRoundedRect(QRectF(2, 2.5, 12, 8.5), 2, 2);
    QPainterPath tail;
    tail.moveTo(4.5, 11);
    tail.lineTo(4.5, 14);
    tail.lineTo(8, 11);
    painter.drawPath(bubble.united(tail));
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const qreal x : {5.0, 8.0, 11.0})
        painter.drawEllipse(QPointF(x, 6.75), 0.9, 0.9);
    painter.restore();
}

void paintCheck(QPainter &painter, const QRectF &box, const QColor &color)
{
    painter.save();
    painter.translate(box.topLeft());
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolyline(QPolygonF({QPointF(3, 8.5), QPointF(6.5, 12), QPointF(13, 4.5)}));
    painter.restore();
}

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
    sparkTimer_.setInterval(sparkStepMs);
    connect(&sparkTimer_, &QTimer::timeout, this, &ClaudeIndicator::advanceLitBranch);
}

void ClaudeIndicator::setStatus(Status status)
{
    if (status == status_)
        return;
    status_ = status;
    setAccessibleName(QStringLiteral("Claude: %1").arg(claudeStatusText(status)));
    if (status == Status::Working)
        sparkTimer_.start();
    else
        sparkTimer_.stop();
    update();
}

void ClaudeIndicator::applyTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

QSize ClaudeIndicator::sizeHint() const
{
    // The widest state sets the width: the bar keeps its layout as the
    // state changes.
    const QFontMetrics metrics = fontMetrics();
    int statusWidth = 0;
    for (const Status status : allStatuses)
        statusWidth = std::max(statusWidth, metrics.horizontalAdvance(claudeStatusText(status)));
    const int width = paddingPx + iconPx + iconGapPx + metrics.horizontalAdvance(QStringLiteral("Claude")) +
                      2 * dividerGapPx + 1 + statusWidth + paddingPx;
    return {width, std::max(iconPx, metrics.height()) + 2 * verticalPaddingPx};
}

void ClaudeIndicator::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor color = claudeStatusColor(status_, theme_);
    paintBackground(painter, color);

    const QFontMetrics metrics = fontMetrics();
    int x = paddingPx;
    paintIcon(painter, QRectF(x, (height() - iconPx) / 2.0, iconPx, iconPx), color);
    x += iconPx + iconGapPx;

    const QString name = QStringLiteral("Claude");
    const QRect textBox(0, 0, width(), height());
    painter.setPen(status_ == Status::Ready ? theme_.mutedText : theme_.foreground);
    painter.drawText(textBox.adjusted(x, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, name);
    x += metrics.horizontalAdvance(name) + dividerGapPx;

    const qreal dividerInset = height() / 4.0;
    painter.setPen(QPen(ThemeStyle::mixed(theme_.window, color, borderShare), 1));
    painter.drawLine(QPointF(x + 0.5, dividerInset), QPointF(x + 0.5, height() - dividerInset));
    x += 1 + dividerGapPx;

    painter.setPen(color);
    painter.drawText(textBox.adjusted(x, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, claudeStatusText(status_));
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

void ClaudeIndicator::paintIcon(QPainter &painter, const QRectF &box, const QColor &color) const
{
    switch (status_) {
    case Status::Ready:
    case Status::Working:
        paintStar(painter, box, color);
        break;
    case Status::NeedsYou:
        paintBubble(painter, box, color);
        break;
    case Status::Done:
        paintCheck(painter, box, color);
        break;
    }
}

// Claude's burst (see UiIcons' "claude"): eight branches, the long ones
// on the axes. While Claude works, each lights up in turn.
void ClaudeIndicator::paintStar(QPainter &painter, const QRectF &box, const QColor &color) const
{
    const QPointF center = box.center();
    const qreal inner = 1.8;
    for (int branch = 0; branch < branchCount; ++branch) {
        QColor branchColor = color;
        if (status_ == Status::Working)
            branchColor.setAlphaF(float(branchOpacity(branch, litBranch_)));
        painter.setPen(QPen(branchColor, 1.5, Qt::SolidLine, Qt::RoundCap));
        const qreal angle = branch * M_PI / 4;
        const qreal outer = branch % 2 ? 5.2 : 6.3;
        const QPointF direction(std::cos(angle), std::sin(angle));
        painter.drawLine(center + inner * direction, center + outer * direction);
    }
}

void ClaudeIndicator::advanceLitBranch()
{
    litBranch_ = (litBranch_ + 1) % branchCount;
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
    // Preferences ▸ Fonts: the width follows the text.
    if (event->type() == QEvent::FontChange)
        updateGeometry();
    QAbstractButton::changeEvent(event);
}
