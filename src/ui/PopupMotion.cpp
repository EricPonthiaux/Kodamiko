// SPDX-License-Identifier: GPL-3.0-only

#include "Motion.h"

#include <QApplication>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>

namespace {

constexpr const char *effectName = "motionFade";
constexpr const char *popupProperty = "duetMotionPopup";
constexpr const char *ghostName = "motionExit";
constexpr const char *exitProperty = "duetMotionExit";

// Painting scales inside the final rectangle: native popups keep their
// placement and their mouse/keyboard grab, including on Wayland.
class PopupEffect : public QGraphicsOpacityEffect {
public:
    PopupEffect(QPointF origin, QObject *parent)
        : QGraphicsOpacityEffect(parent)
        , origin_(origin)
    {
    }

    void setProgress(qreal progress)
    {
        progress_ = progress;
        setOpacity(std::min(1.0, progress * 2));
        update();
    }

protected:
    void draw(QPainter *painter) override
    {
        QPoint offset;
        const QPixmap source = sourcePixmap(Qt::LogicalCoordinates, &offset, NoPad);
        if (source.isNull())
            return;
        const QSizeF size = source.deviceIndependentSize();
        const QPointF pivot(offset.x() + size.width() * origin_.x(), offset.y() + size.height() * origin_.y());
        const qreal scale = 0.96 + 0.04 * progress_;
        painter->save();
        painter->setOpacity(opacity());
        painter->translate(pivot);
        painter->scale(scale, scale);
        painter->translate(-pivot);
        painter->drawPixmap(offset, source);
        painter->restore();
    }

private:
    QPointF origin_;
    qreal progress_ = 0;
};

bool isPanel(const QWidget *widget)
{
    return widget->inherits("OverlayPopup") || widget->windowType() == Qt::Dialog;
}

QPointF originFor(const QWidget *widget)
{
    if (isPanel(widget))
        return {0.5, 0.5};
    const QWidget *parent = widget->parentWidget();
    if (!parent)
        return {0, 0};
    const QPoint center = widget->mapFromGlobal(parent->mapToGlobal(parent->rect().center()));
    return {center.x() > widget->width() / 2 ? 1.0 : 0.0, center.y() > widget->height() / 2 ? 1.0 : 0.0};
}

void clearEffect(QWidget *widget)
{
    if (widget->graphicsEffect() && widget->graphicsEffect()->objectName() == QLatin1String(effectName))
        widget->setGraphicsEffect(nullptr);
}

void enterPopup(QWidget *widget)
{
    if (!Motion::policy().isEnabled() || !Motion::canAnimate(widget))
        return;
    auto *effect = new PopupEffect(originFor(widget), widget);
    effect->setObjectName(QLatin1String(effectName));
    effect->setProgress(0);
    widget->setGraphicsEffect(effect);
    auto *animation = new QVariantAnimation(effect);
    animation->setDuration(int((isPanel(widget) ? Motion::panelEnterDuration : Motion::enterDuration).count()));
    animation->setEasingCurve(Motion::enterCurve());
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    QObject::connect(animation, &QVariantAnimation::valueChanged, effect,
                     [effect](const QVariant &value) { effect->setProgress(value.toReal()); });
    // The effect owns the animation: remove it after its finished signal.
    QObject::connect(
        animation, &QAbstractAnimation::finished, effect, [widget] { clearEffect(widget); }, Qt::QueuedConnection);
    Motion::track(animation);
    animation->start();
}

// No new native window on exit: that could steal a grab or be repositioned
// by the compositor. Outside the owner's client area, dismissal is immediate.
QWidget *exitOwner(const QWidget *widget)
{
    const QWidget *owner = widget->parentWidget();
    while (owner && owner->isWindow() && owner->windowType() != Qt::Window)
        owner = owner->parentWidget();
    if (!owner)
        owner = QApplication::activeWindow();
    if (!owner || owner == widget)
        return nullptr;
    const QWidget *window = owner->window();
    const QRect area(window->mapFromGlobal(widget->mapToGlobal(QPoint())), widget->size());
    return window->isVisible() && window->rect().contains(area) ? owner->window() : nullptr;
}

void exitPopup(QWidget *widget)
{
    QWidget *owner = exitOwner(widget);
    if (!owner || !Motion::policy().isEnabled() || !Motion::canAnimate(widget))
        return;
    const QPixmap snapshot = widget->grab();
    if (snapshot.isNull())
        return;
    auto *picture = new QLabel(owner);
    picture->setObjectName(QLatin1String(ghostName));
    picture->setAttribute(Qt::WA_TransparentForMouseEvents);
    picture->setAttribute(Qt::WA_NoSystemBackground);
    picture->setPixmap(snapshot);
    picture->setGeometry(QRect(owner->mapFromGlobal(widget->mapToGlobal(QPoint())), widget->size()));
    auto *effect = new PopupEffect(originFor(widget), picture);
    effect->setProgress(1);
    picture->setGraphicsEffect(effect);
    auto *animation = new QVariantAnimation(picture);
    animation->setDuration(int((isPanel(widget) ? Motion::panelExitDuration : Motion::exitDuration).count()));
    animation->setEasingCurve(Motion::exitCurve());
    animation->setStartValue(1.0);
    animation->setEndValue(0.0);
    QObject::connect(animation, &QVariantAnimation::valueChanged, effect,
                     [effect](const QVariant &value) { effect->setProgress(value.toReal()); });
    QObject::connect(animation, &QAbstractAnimation::finished, picture, &QObject::deleteLater);
    widget->setProperty(exitProperty, QVariant::fromValue(QPointer<QWidget>(picture)));
    Motion::track(animation);
    picture->show();
    picture->raise();
    animation->start();
}

void cancelExit(QWidget *widget)
{
    const auto picture = widget->property(exitProperty).value<QPointer<QWidget>>();
    if (picture) {
        picture->hide();
        picture->deleteLater();
    }
    widget->setProperty(exitProperty, {});
}

bool isPopup(const QWidget *widget)
{
    if (widget->objectName() == QLatin1String(ghostName))
        return false;
    const Qt::WindowType type = widget->windowType();
    return widget->property(popupProperty).toBool() || type == Qt::Popup || type == Qt::ToolTip || type == Qt::Dialog;
}

class PopupWatcher : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const QEvent::Type type = event->type();
        if ((type != QEvent::Show && type != QEvent::Hide) || event->spontaneous() || !watched->isWidgetType())
            return false;
        auto *widget = static_cast<QWidget *>(watched);
        if (!isPopup(widget))
            return false;
        if (type == QEvent::Show) {
            cancelExit(widget);
            enterPopup(widget);
        } else {
            exitPopup(widget);
            clearEffect(widget);
        }
        return false;
    }
};

} // namespace

namespace Motion {

void install()
{
    Q_ASSERT(qApp);
    if (qApp->findChild<QObject *>(QStringLiteral("motionWatcher")))
        return;
    auto *watcher = new PopupWatcher(qApp);
    watcher->setObjectName(QStringLiteral("motionWatcher"));
    qApp->installEventFilter(watcher);
    // Qt's built-in transitions would animate the same popup twice.
    QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);
    QApplication::setEffectEnabled(Qt::UI_FadeMenu, false);
    QApplication::setEffectEnabled(Qt::UI_AnimateCombo, false);
    QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, false);
    QApplication::setEffectEnabled(Qt::UI_FadeTooltip, false);
}

void watchPopup(QWidget *widget)
{
    install();
    widget->setProperty(popupProperty, true);
}

} // namespace Motion
