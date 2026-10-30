// SPDX-License-Identifier: GPL-3.0-only

#include "MergeGutter.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

namespace {

constexpr int gutterWidthPx = 30;
constexpr int markSidePx = 20;
constexpr qreal markRadiusPx = 4;
constexpr qreal strokeWidthPx = 1.6;

// A chevron pointing `direction`, in `rect`.
QPainterPath chevron(const QRectF &rect, MergeGutter::Direction direction)
{
    const qreal tip = direction == MergeGutter::Direction::Right ? rect.right() : rect.left();
    const qreal back = direction == MergeGutter::Direction::Right ? rect.left() : rect.right();
    QPainterPath path;
    path.moveTo(back, rect.top());
    path.lineTo(tip, rect.center().y());
    path.lineTo(back, rect.bottom());
    return path;
}

QPainterPath checkMark(const QRectF &rect)
{
    QPainterPath path;
    path.moveTo(rect.left(), rect.center().y());
    path.lineTo(rect.left() + rect.width() * 0.4, rect.bottom());
    path.lineTo(rect.right(), rect.top());
    return path;
}

} // namespace

MergeGutter::MergeGutter(Direction direction, QString sideName, Theme theme, QWidget *parent)
    : QWidget(parent)
    , direction_(direction)
    , sideName_(std::move(sideName))
    , theme_(std::move(theme))
{
    setFixedWidth(gutterWidthPx);
    setMouseTracking(true);
}

void MergeGutter::setMarks(QList<Mark> marks, int rowHeight)
{
    marks_ = std::move(marks);
    rowHeight_ = rowHeight;
    update();
}

void MergeGutter::applyTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

QSize MergeGutter::sizeHint() const
{
    return {gutterWidthPx, 0};
}

bool MergeGutter::event(QEvent *event)
{
    if (event->type() != QEvent::ToolTip)
        return QWidget::event(event);
    const auto *help = static_cast<QHelpEvent *>(event);
    const Mark *mark = markAt(help->pos());
    if (!mark) {
        QToolTip::hideText();
        return true;
    }
    QToolTip::showText(help->globalPos(), mark->isTaken ? tr("The result has %1's lines").arg(sideName_)
                                                        : tr("Take %1's lines into the result").arg(sideName_));
    return true;
}

void MergeGutter::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), theme_.window);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const Mark &mark : std::as_const(marks_)) {
        const QRectF box = markRect(mark);
        if (!rect().intersects(box.toRect()))
            continue;
        painter.setPen(QPen(theme_.border, 1));
        painter.setBrush(mark.isTaken ? theme_.window : theme_.button);
        painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), markRadiusPx, markRadiusPx);
        painter.setBrush(Qt::NoBrush);
        const QColor stroke = mark.isTaken ? theme_.mutedText : theme_.foreground;
        painter.setPen(QPen(stroke, strokeWidthPx, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QRectF glyph = box.adjusted(7, 6, -7, -6);
        painter.drawPath(mark.isTaken ? checkMark(glyph.adjusted(-1, 0, 1, 0)) : chevron(glyph, direction_));
    }
}

void MergeGutter::mousePressEvent(QMouseEvent *event)
{
    const Mark *mark = markAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && mark && !mark->isTaken)
        emit takeRequested(mark->segment);
}

QRect MergeGutter::markRect(const Mark &mark) const
{
    const int top = mark.y + std::max(0, (rowHeight_ - markSidePx) / 2);
    return {(width() - markSidePx) / 2, top, markSidePx, markSidePx};
}

const MergeGutter::Mark *MergeGutter::markAt(QPoint position) const
{
    for (const Mark &mark : marks_) {
        if (markRect(mark).contains(position))
            return &mark;
    }
    return nullptr;
}
