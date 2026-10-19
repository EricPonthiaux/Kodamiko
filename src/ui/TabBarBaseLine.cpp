#include "TabBarBaseLine.h"

#include <QEvent>
#include <QPainter>
#include <QTabBar>

TabBarBaseLine::TabBarBaseLine(QTabBar *bar)
    : QWidget(bar)
    , bar_(bar)
    , color_(Qt::gray)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    bar_->setDrawBase(false);
    bar_->installEventFilter(this);
    connect(bar_, &QTabBar::currentChanged, this, qOverload<>(&QWidget::update));
    connect(bar_, &QTabBar::tabMoved, this, qOverload<>(&QWidget::update));
    setGeometry(bar_->rect());
}

void TabBarBaseLine::setColor(const QColor &color)
{
    color_ = color;
    update();
}

bool TabBarBaseLine::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == bar_ && event->type() == QEvent::Resize) {
        setGeometry(bar_->rect());
        raise(); // above the close buttons, created after it
    }
    return QWidget::eventFilter(watched, event);
}

void TabBarBaseLine::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setPen(color_);
    const int y = height() - 1;
    if (bar_->count() == 0)
        return;
    const QRect current = bar_->tabRect(bar_->currentIndex());
    if (current.left() > 0)
        painter.drawLine(0, y, current.left(), y);
    painter.drawLine(current.right(), y, width(), y);
}
