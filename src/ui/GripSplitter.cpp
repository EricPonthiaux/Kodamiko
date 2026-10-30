// SPDX-License-Identifier: GPL-3.0-only

#include "GripSplitter.h"

#include "HoverProgress.h"
#include "ThemeStyle.h"

#include <QEvent>
#include <QPainter>
#include <QSplitterHandle>

#include <optional>

namespace {

constexpr int handleSize = 7;
constexpr int gripLength = 32;
constexpr int gripWidth = 5;

class GripHandle : public QSplitterHandle {
public:
    GripHandle(Qt::Orientation orientation, QSplitter *parent)
        : QSplitterHandle(orientation, parent)
    {
        setAttribute(Qt::WA_Hover);
    }

    // Fixed, whatever handle width a saved splitter state brings back.
    QSize sizeHint() const override { return {handleSize, handleSize}; }

    void setRule(const GripSplitter::HandleRule &rule)
    {
        rule_ = rule;
        update();
    }

    void setSides(const GripSplitter::HandleSides &sides)
    {
        sides_ = sides;
        update();
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave)
            hover_.setHovered(event->type() == QEvent::HoverEnter);
        return QSplitterHandle::event(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QPalette &colors = palette();
        painter.fillRect(rect(), colors.color(QPalette::Window));

        const bool horizontal = orientation() == Qt::Horizontal; // side by side: a vertical line
        const QPointF center = QRectF(rect()).center();
        if (horizontal && sides_)
            paintSides(painter, center.x());
        painter.setPen(QPen(colors.color(QPalette::Mid), 1));
        if (horizontal)
            painter.drawLine(QPointF(center.x(), 0), QPointF(center.x(), height()));
        else
            painter.drawLine(QPointF(0, center.y()), QPointF(width(), center.y()));
        if (horizontal && rule_.y >= 0)
            paintRule(painter, center.x());

        SplitterGrip::paint(painter, *this, orientation(), hover_.progress());
    }

private:
    void paintRule(QPainter &painter, qreal lineX) const
    {
        const qreal from = rule_.sides.testFlag(Qt::LeftEdge) ? 0 : lineX;
        const qreal to = rule_.sides.testFlag(Qt::RightEdge) ? width() : lineX;
        painter.fillRect(QRectF(from, rule_.y, to - from, 1), rule_.color);
    }

    void paintSides(QPainter &painter, qreal lineX) const
    {
        const GripSplitter::HandleSides &sides = *sides_;
        const QRectF left(0, 0, lineX, height());
        const QRectF right(lineX, 0, width() - lineX, height());
        const auto fill = [&painter](const QRectF &area, const QColor &color) {
            if (color.isValid())
                painter.fillRect(area, color);
        };
        const qreal border = sides.split - sides.borderWidth;
        fill(left.adjusted(0, 0, 0, sides.split - height()), sides.leftHeader);
        fill(right.adjusted(0, 0, 0, sides.split - height()), sides.rightHeader);
        fill(QRectF(left.left(), border, left.width(), sides.borderWidth), sides.leftBorder);
        fill(QRectF(right.left(), border, right.width(), sides.borderWidth), sides.rightBorder);
        fill(left.adjusted(0, sides.split, 0, 0), sides.leftContent);
        fill(right.adjusted(0, sides.split, 0, 0), sides.rightContent);
    }

    GripSplitter::HandleRule rule_;
    std::optional<GripSplitter::HandleSides> sides_;
    HoverProgress hover_{this};
};

} // namespace

GripSplitter::GripSplitter(Qt::Orientation orientation, QWidget *parent)
    : QSplitter(orientation, parent)
{
    setHandleWidth(handleSize);
}

void GripSplitter::setHandleRule(int index, const HandleRule &rule)
{
    if (auto *gripHandle = static_cast<GripHandle *>(handle(index)))
        gripHandle->setRule(rule);
}

void GripSplitter::setHandleSides(int index, const HandleSides &sides)
{
    if (auto *gripHandle = static_cast<GripHandle *>(handle(index)))
        gripHandle->setSides(sides);
}

QSplitterHandle *GripSplitter::createHandle()
{
    return new GripHandle(orientation(), this);
}

void SplitterGrip::paint(QPainter &painter, const QWidget &handle, Qt::Orientation orientation, qreal highlight)
{
    const bool horizontal = orientation == Qt::Horizontal;
    QRectF grip(0, 0, horizontal ? gripWidth : gripLength, horizontal ? gripLength : gripWidth);
    grip.moveCenter(QRectF(handle.rect()).center());
    const QPalette &colors = handle.palette();
    const QColor accent = colors.color(QPalette::Accent);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(ThemeStyle::mixed(colors.color(QPalette::Mid), accent, highlight), 1));
    painter.setBrush(ThemeStyle::mixed(colors.color(QPalette::Button), accent, highlight));
    painter.drawRoundedRect(grip.adjusted(0.5, 0.5, -0.5, -0.5), gripWidth / 2.0, gripWidth / 2.0);
    painter.restore();
}
