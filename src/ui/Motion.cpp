// SPDX-License-Identifier: GPL-3.0-only

#include "Motion.h"

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPointer>
#include <QPropertyAnimation>
#include <QWidget>

#include <algorithm>
#include <functional>

namespace {

// Names the opacity effects of these fades, to tell them from a widget's own.
const char *const fadeEffectName = "motionFade";

bool drawsOutsidePainter(const QObject *widget)
{
    return widget->inherits("QOpenGLWidget") || widget->inherits("QQuickWidget") || widget->inherits("QRhiWidget") ||
           widget->inherits("QWebEngineView");
}

} // namespace

namespace Motion {

bool isOnScreen(const QWidget *widget)
{
    return widget->isVisible() && !widget->window()->isMinimized();
}

bool canAnimate(const QWidget *widget)
{
    const QGraphicsEffect *effect = widget->graphicsEffect();
    if (effect && effect->objectName() != QLatin1String(fadeEffectName))
        return false;
    if (drawsOutsidePainter(widget))
        return false;
    return std::ranges::none_of(widget->findChildren<QWidget *>(), drawsOutsidePainter);
}

} // namespace Motion

namespace {

QEasingCurve bezier(QPointF first, QPointF second)
{
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(first, second, QPointF(1, 1));
    return curve;
}

// Animates a new opacity effect on `widget` from `from` to `to`; `finished`
// runs at the end, unless the effect is replaced before.
void animateOpacity(QWidget *widget, qreal from, qreal to, const std::function<void()> &finished)
{
    auto *effect = new QGraphicsOpacityEffect(widget);
    effect->setObjectName(QLatin1String(fadeEffectName));
    effect->setOpacity(from);
    widget->setGraphicsEffect(effect); // which owns it, and deletes the one before
    auto *animation = new QPropertyAnimation(effect, "opacity", effect);
    animation->setDuration(int(Motion::fadeDuration.count()));
    animation->setStartValue(from);
    animation->setEndValue(to);
    animation->setEasingCurve(to > from ? Motion::enterCurve() : Motion::exitCurve());
    // A replaced effect stops its animation without `finished`.
    QObject::connect(animation, &QAbstractAnimation::finished, effect, finished, Qt::QueuedConnection);
    Motion::track(animation);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace

namespace Motion {

Policy::Policy(QObject *parent)
    : QObject(parent)
{
}

void Policy::setEnabled(bool enabled)
{
    if (enabled_ == enabled)
        return;
    enabled_ = enabled;
    emit enabledChanged(enabled);
}

Policy &policy()
{
    static QPointer<Policy> instance;
    if (!instance)
        instance = new Policy(qApp);
    return *instance;
}

QEasingCurve standardCurve()
{
    // Material 3 easing tokens:
    // https://github.com/material-components/material-components-android/blob/master/docs/theming/Motion.md
    return bezier({0.2, 0}, {0, 1});
}

QEasingCurve enterCurve()
{
    return bezier({0.05, 0.7}, {0.1, 1});
}

QEasingCurve exitCurve()
{
    return bezier({0.3, 0}, {0.8, 0.15});
}

void track(QAbstractAnimation *animation)
{
    QObject::connect(&policy(), &Policy::enabledChanged, animation, [animation](bool enabled) {
        if (!enabled && animation->state() != QAbstractAnimation::Stopped)
            animation->setCurrentTime(animation->totalDuration());
    });
}

void fadeIn(QWidget *widget)
{
    if (!policy().isEnabled() || !canAnimate(widget))
        return;
    animateOpacity(widget, 0.0, 1.0, [widget] { widget->setGraphicsEffect(nullptr); });
}

void fadeOut(QWidget *widget)
{
    QWidget *parent = widget->parentWidget();
    if (!policy().isEnabled() || !parent || !widget->isVisible() || !canAnimate(widget))
        return;
    auto *picture = new QLabel(parent); // which owns it, until it deletes itself
    picture->setAttribute(Qt::WA_TransparentForMouseEvents);
    picture->setPixmap(widget->grab());
    picture->setGeometry(widget->geometry());
    picture->show();
    picture->raise();
    const QPointer<QLabel> guard(picture);
    animateOpacity(picture, 1.0, 0.0, [guard] {
        if (guard)
            guard->deleteLater();
    });
}

} // namespace Motion
