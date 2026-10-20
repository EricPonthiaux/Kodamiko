// SPDX-License-Identifier: GPL-3.0-only

#include "TrailingTabButton.h"

#include <QAbstractButton>
#include <QEvent>
#include <QTabBar>
#include <QTabWidget>

#include <algorithm>

namespace {

constexpr int gap = 4;

// Square, as wide as `button` would be, rather than as tall as the bar.
// Of the bar's height parity: centered on it to the pixel.
QSize buttonSize(const QAbstractButton *button, int barHeight)
{
    int side = std::min(button->sizeHint().width(), barHeight);
    if ((barHeight - side) % 2 != 0)
        ++side;
    return {side, side};
}

} // namespace

TrailingTabButton::TrailingTabButton(QTabWidget *tabs, QAbstractButton *button)
    : QObject(tabs)
    , tabs_(tabs)
    , button_(button)
    , room_(new QWidget(tabs))
{
    button_->setParent(tabs_);
    // The tab bar stops before this empty corner: room for the button when
    // the tabs fill the bar.
    room_->setFixedWidth(button_->sizeHint().width() + gap);
    tabs_->setCornerWidget(room_, Qt::TopRightCorner);
    tabs_->tabBar()->installEventFilter(this);
    button_->raise();
    button_->show();
}

void TrailingTabButton::setEndButton(QAbstractButton *button)
{
    endButton_ = button;
    endButton_->setParent(tabs_);
    endButton_->raise();
    endButton_->show();
    place();
}

bool TrailingTabButton::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::Paint: // after a tab is added, closed or renamed
        if (!placing_) {
            placing_ = true;
            QMetaObject::invokeMethod(this, &TrailingTabButton::place, Qt::QueuedConnection);
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void TrailingTabButton::place()
{
    placing_ = false;
    const QTabBar *bar = tabs_->tabBar();
    button_->setVisible(bar->isVisible());
    const QRect barRect = bar->geometry();
    const QSize size = buttonSize(button_, barRect.height());
    const QSize endSize = endButton_ ? buttonSize(endButton_, barRect.height()) : QSize();
    const int top = barRect.top() + (barRect.height() - size.height()) / 2;
    // The icons may have come since.
    room_->setFixedWidth(size.width() + gap + (endButton_ ? endSize.width() + gap : 0));
    if (endButton_) {
        endButton_->setVisible(bar->isVisible());
        const int endTop = barRect.top() + (barRect.height() - endSize.height()) / 2;
        const QRect end(QPoint(tabs_->width() - endSize.width() - gap, endTop), endSize);
        if (endButton_->geometry() != end)
            endButton_->setGeometry(end);
    }
    int x = barRect.left() + gap;
    if (bar->count() > 0)
        x = barRect.left() + bar->tabRect(bar->count() - 1).right() + 1 + gap;
    x = std::min(x, barRect.right() + 1 + gap); // the tabs fill the bar: in the kept room
    const QRect geometry(QPoint(x, top), size);
    if (button_->geometry() != geometry)
        button_->setGeometry(geometry);
}
