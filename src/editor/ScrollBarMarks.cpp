// SPDX-License-Identifier: GPL-3.0-only

#include "ScrollBarMarks.h"

#include <QEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyleOptionSlider>

namespace {

constexpr int markHeight = 2;
constexpr int markInset = 2; // from the scroll bar's sides

} // namespace

ScrollBarMarks::ScrollBarMarks(QScrollBar *scrollBar)
    : QWidget(scrollBar)
    , scrollBar_(scrollBar)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    scrollBar_->installEventFilter(this);
    setGeometry(scrollBar_->rect());
    hide();
}

void ScrollBarMarks::setMarks(const QList<qreal> &positions, const QColor &color)
{
    positions_ = positions;
    color_ = color;
    setVisible(!positions_.isEmpty());
    raise();
    update();
}

void ScrollBarMarks::clear()
{
    setMarks({}, color_);
}

bool ScrollBarMarks::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == scrollBar_ && event->type() == QEvent::Resize)
        setGeometry(scrollBar_->rect());
    return QWidget::eventFilter(watched, event);
}

void ScrollBarMarks::paintEvent(QPaintEvent * /*event*/)
{
    // The groove stands for the whole document, between the arrows if the
    // style draws any.
    QStyleOptionSlider option;
    option.initFrom(scrollBar_);
    option.orientation = scrollBar_->orientation();
    option.minimum = scrollBar_->minimum();
    option.maximum = scrollBar_->maximum();
    option.sliderPosition = scrollBar_->sliderPosition();
    option.sliderValue = scrollBar_->value();
    option.pageStep = scrollBar_->pageStep();
    option.singleStep = scrollBar_->singleStep();
    const QRect groove =
        scrollBar_->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarGroove, scrollBar_);
    const int usableHeight = std::max(groove.height() - markHeight, 0);

    QPainter painter(this);
    for (const qreal position : positions_) {
        const int y = groove.top() + qRound(std::clamp(position, 0.0, 1.0) * usableHeight);
        painter.fillRect(QRect(markInset, y, width() - (2 * markInset), markHeight), color_);
    }
}
