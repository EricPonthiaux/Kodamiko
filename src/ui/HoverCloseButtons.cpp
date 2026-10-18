#include "HoverCloseButtons.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHoverEvent>
#include <QStyle>
#include <QTabBar>

namespace {

void setShown(QWidget *button, bool shown)
{
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(button->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(button);
        button->setGraphicsEffect(effect); // which owns it
    }
    effect->setOpacity(shown ? 1.0 : 0.0);
}

} // namespace

HoverCloseButtons::HoverCloseButtons(QTabBar *bar)
    : QObject(bar)
    , bar_(bar)
{
    bar_->installEventFilter(this);
    connect(bar_, &QTabBar::currentChanged, this, &HoverCloseButtons::update);
    connect(bar_, &QTabBar::tabMoved, this, &HoverCloseButtons::update);
    update();
}

bool HoverCloseButtons::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::HoverEnter:
    case QEvent::HoverMove:
        hovered_ = bar_->tabAt(static_cast<QHoverEvent *>(event)->position().toPoint());
        update();
        break;
    case QEvent::HoverLeave:
        hovered_ = -1;
        update();
        break;
    case QEvent::ChildAdded:
        // A new tab's button: once QTabBar has placed it.
        QMetaObject::invokeMethod(this, &HoverCloseButtons::update, Qt::QueuedConnection);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void HoverCloseButtons::update()
{
    const auto closeSide = static_cast<QTabBar::ButtonPosition>(
        bar_->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, bar_));
    for (int i = 0; i < bar_->count(); ++i) {
        if (QWidget *button = bar_->tabButton(i, closeSide))
            setShown(button, i == bar_->currentIndex() || i == hovered_);
    }
}
