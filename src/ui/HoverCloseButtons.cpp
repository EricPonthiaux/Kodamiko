// SPDX-License-Identifier: GPL-3.0-only

#include "HoverCloseButtons.h"

#include "Motion.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHoverEvent>
#include <QPropertyAnimation>
#include <QStyle>
#include <QTabBar>

namespace {

// A new button takes its state at once; a known one eases into it.
void setShown(QWidget *button, bool shown)
{
    const qreal opacity = shown ? 1.0 : 0.0;
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(button->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(button);
        effect->setOpacity(opacity);
        button->setGraphicsEffect(effect); // which owns it
        return;
    }
    auto *fade = effect->findChild<QPropertyAnimation *>();
    const bool isHeadingThere = fade ? fade->endValue().toReal() == opacity : effect->opacity() == opacity;
    if (!Motion::policy().isEnabled()) {
        if (fade)
            fade->stop();
        effect->setOpacity(opacity);
        return;
    }
    if (isHeadingThere)
        return;
    if (!fade) {
        fade = new QPropertyAnimation(effect, "opacity", effect);
        fade->setDuration(int(Motion::hoverDuration.count()));
        fade->setEasingCurve(Motion::standardCurve());
        Motion::track(fade);
    }
    fade->stop();
    fade->setStartValue(effect->opacity());
    fade->setEndValue(opacity);
    fade->start();
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
