// SPDX-License-Identifier: GPL-3.0-only

#include "ConflictSteps.h"

#include "ui/UiMetrics.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

namespace {

constexpr int dotPx = 20;
constexpr int dotGapPx = 5;
// Around the dots: room for the ring of the one shown.
constexpr int ringMarginPx = 4;
constexpr qreal ringOffsetPx = 2.5;
// Beyond, the dots would be too small to tell apart: the label says it.
constexpr int maxDots = 12;

} // namespace

ConflictSteps::ConflictSteps(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
}

void ConflictSteps::setTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

void ConflictSteps::setConflicts(const QList<ConflictState> &states, int current)
{
    states_ = states;
    current_ = current;
    setVisible(!states.isEmpty() && states.size() <= maxDots);
    setMinimumSize(sizeHint());
    updateGeometry();
    update();
}

QSize ConflictSteps::sizeHint() const
{
    const int count = int(states_.size());
    const int margins = 2 * UiMetrics::scaled(ringMarginPx);
    return {count * UiMetrics::scaled(dotPx) + std::max(0, count - 1) * UiMetrics::scaled(dotGapPx) + margins,
            UiMetrics::scaled(dotPx) + margins};
}

bool ConflictSteps::event(QEvent *event)
{
    if (event->type() != QEvent::ToolTip)
        return QWidget::event(event);
    const auto *help = static_cast<QHelpEvent *>(event);
    const int index = indexAt(help->pos());
    if (index < 0) {
        QToolTip::hideText();
        return true;
    }
    QToolTip::showText(help->globalPos(),
                       tr("Conflict %1: %2").arg(index + 1).arg(ConflictStates::name(states_[index])));
    return true;
}

void ConflictSteps::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < states_.size(); ++i)
        paintDot(painter, i);
}

void ConflictSteps::mousePressEvent(QMouseEvent *event)
{
    const int index = indexAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && index >= 0)
        emit conflictRequested(index);
}

QRectF ConflictSteps::dotRect(int index) const
{
    const int side = UiMetrics::scaled(dotPx);
    const int x = UiMetrics::scaled(ringMarginPx) + index * (side + UiMetrics::scaled(dotGapPx));
    return QRectF(x, (height() - side) / 2.0, side, side).adjusted(1.5, 1.5, -1.5, -1.5);
}

int ConflictSteps::indexAt(QPoint position) const
{
    for (int i = 0; i < states_.size(); ++i) {
        if (dotRect(i).adjusted(-2, -2, 2, 2).contains(position))
            return i;
    }
    return -1;
}

// Open: a ring with the number; resolved: a filled dot with a check.
void ConflictSteps::paintDot(QPainter &painter, int index) const
{
    const QRectF dot = dotRect(index);
    const QColor color = ConflictStates::color(states_[index], theme_);
    const bool isResolved = states_[index] == ConflictState::Resolved;
    if (index == current_) {
        painter.setPen(QPen(theme_.foreground, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(dot.adjusted(-ringOffsetPx, -ringOffsetPx, ringOffsetPx, ringOffsetPx));
    }
    painter.setPen(QPen(color, 1.5));
    painter.setBrush(isResolved ? color : Qt::transparent);
    painter.drawEllipse(dot);
    QFont font = this->font();
    font.setPixelSize(int(dot.height() * 0.62));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(isResolved ? theme_.window : theme_.foreground);
    painter.drawText(dot, Qt::AlignCenter, isResolved ? QStringLiteral("✓") : QString::number(index + 1));
}
