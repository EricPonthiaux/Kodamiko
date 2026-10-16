#include "GripSplitter.h"

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

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave)
            update();
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

        QRectF grip(0, 0, horizontal ? gripWidth : gripLength, horizontal ? gripLength : gripWidth);
        grip.moveCenter(center);
        const bool hovered = underMouse();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(hovered ? colors.color(QPalette::Accent) : colors.color(QPalette::Mid), 1));
        painter.setBrush(hovered ? colors.color(QPalette::Accent) : colors.color(QPalette::Button));
        painter.drawRoundedRect(grip.adjusted(0.5, 0.5, -0.5, -0.5), gripWidth / 2.0, gripWidth / 2.0);
    }
};

} // namespace

GripSplitter::GripSplitter(Qt::Orientation orientation, QWidget *parent)
    : QSplitter(orientation, parent)
{
    setHandleWidth(handleSize);
}

QSplitterHandle *GripSplitter::createHandle()
{
    return new GripHandle(orientation(), this);
}
