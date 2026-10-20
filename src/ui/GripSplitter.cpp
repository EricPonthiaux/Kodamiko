#include "GripSplitter.h"

#include "HoverProgress.h"
#include "ThemeStyle.h"

#include <QEvent>
#include <QPainter>
#include <QSplitterHandle>

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
        painter.setPen(QPen(colors.color(QPalette::Mid), 1));
        if (horizontal)
            painter.drawLine(QPointF(center.x(), 0), QPointF(center.x(), height()));
        else
            painter.drawLine(QPointF(0, center.y()), QPointF(width(), center.y()));
        if (horizontal && rule_.y >= 0)
            paintRule(painter, center.x());

        QRectF grip(0, 0, horizontal ? gripWidth : gripLength, horizontal ? gripLength : gripWidth);
        grip.moveCenter(center);
        const QColor accent = colors.color(QPalette::Accent);
        const qreal highlight = hover_.progress();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(ThemeStyle::mixed(colors.color(QPalette::Mid), accent, highlight), 1));
        painter.setBrush(ThemeStyle::mixed(colors.color(QPalette::Button), accent, highlight));
        painter.drawRoundedRect(grip.adjusted(0.5, 0.5, -0.5, -0.5), gripWidth / 2.0, gripWidth / 2.0);
    }

private:
    void paintRule(QPainter &painter, qreal lineX) const
    {
        const qreal from = rule_.sides.testFlag(Qt::LeftEdge) ? 0 : lineX;
        const qreal to = rule_.sides.testFlag(Qt::RightEdge) ? width() : lineX;
        painter.fillRect(QRectF(from, rule_.y, to - from, 1), rule_.color);
    }

    GripSplitter::HandleRule rule_;
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

QSplitterHandle *GripSplitter::createHandle()
{
    return new GripHandle(orientation(), this);
}
