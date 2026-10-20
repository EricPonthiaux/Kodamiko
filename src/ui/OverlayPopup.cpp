// SPDX-License-Identifier: GPL-3.0-only

#include "OverlayPopup.h"

#include "Motion.h"
#include "WindowFrame.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QWindow>

namespace {

constexpr int radius = 4;
// The shadow spreads this far, within the room kept for resizing
// (shadowWidth), and drops this much below the panel.
constexpr int shadowSpread = 8;
constexpr int shadowDrop = 2;
constexpr int windowMargin = 24; // between the panel and the window's edges

} // namespace

class OverlayPopup::Watcher : public QObject {
public:
    explicit Watcher(OverlayPopup *popup)
        : QObject(popup)
        , popup_(popup)
    {
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        popup_->onWatchedEvent(watched, event);
        return false;
    }

private:
    OverlayPopup *popup_;
};

OverlayPopup::OverlayPopup(const QString &settingsKey, const QSizeF &share, const QSize &minimum, QWidget *parent)
    : QFrame(parent->window())
    // "overlays/": sizes saved before the panels could be resized are ignored.
    , settingsKey_(QStringLiteral("overlays/") + settingsKey)
    , share_(share)
    , minimum_(minimum)
    , watcher_(new Watcher(this))
{
    setFrameShape(QFrame::NoFrame);
    applyMargins();
    setMouseTracking(true); // the cursor over the edges
    hide();
    Motion::watchPopup(this);
}

void OverlayPopup::applyMargins()
{
    setContentsMargins(shadowWidth, shadowWidth, shadowWidth, shadowWidth);
}

bool OverlayPopup::event(QEvent *event)
{
    const bool result = QFrame::event(event);
    // A style sheet resets the margins of the widgets it polishes (at each
    // change of theme): the shadow's room is given back.
    if ((event->type() == QEvent::StyleChange || event->type() == QEvent::Polish) &&
        contentsMargins() != QMargins(shadowWidth, shadowWidth, shadowWidth, shadowWidth))
        applyMargins();
    return result;
}

QRect OverlayPopup::availableArea() const
{
    const QWidget *window = parentWidget();
    return window->contentsRect().adjusted(windowMargin, windowMargin, -windowMargin, -windowMargin);
}

// The panel's size, shadow excluded: the user's, or a share of the window.
QSize OverlayPopup::panelSize() const
{
    const QRect area = availableArea();
    QSize size = QSettings().value(settingsKey_).toSize();
    if (!size.isValid())
        size = QSize(qRound(area.width() * share_.width()), qRound(area.height() * share_.height()));
    return size.expandedTo(minimum_).boundedTo(area.size());
}

QRect OverlayPopup::placedGeometry() const
{
    const QRect area = availableArea();
    const QSize size = panelSize();
    const int x = area.left() + (area.width() - size.width()) / 2;
    // A panel sits a little above the middle (the optical center); a
    // transient one, a search, right in the middle.
    const int y = behavior_ == Behavior::Transient ? area.top() + (area.height() - size.height()) / 2
                                                   : area.top() + std::max((area.height() - size.height()) / 3, 0);
    const QRect centered = QRect(QPoint(x, y), size).adjusted(-shadowWidth, -shadowWidth, shadowWidth, shadowWidth);
    if (behavior_ == Behavior::Transient)
        return keptInside(centered);
    return keptInside(centered.translated(QSettings().value(offsetKey()).toPoint()));
}

QRect OverlayPopup::keptInside(QRect geometry) const
{
    const QRect bounds = availableArea().adjusted(-shadowWidth, -shadowWidth, shadowWidth, shadowWidth);
    geometry.moveLeft(
        std::clamp(geometry.left(), bounds.left(), std::max(bounds.left(), bounds.right() - geometry.width() + 1)));
    geometry.moveTop(
        std::clamp(geometry.top(), bounds.top(), std::max(bounds.top(), bounds.bottom() - geometry.height() + 1)));
    return geometry;
}

void OverlayPopup::place()
{
    setGeometry(placedGeometry());
}

void OverlayPopup::showCentered()
{
    if (!isVisible()) {
        previousFocus_ = QApplication::focusWidget();
        applyMargins();
        place();
        parentWidget()->installEventFilter(watcher_); // window resized
        // The window's own events: a press or a move anywhere in it,
        // whichever widget takes it.
        if (parentWidget()->windowHandle())
            parentWidget()->windowHandle()->installEventFilter(watcher_);
    }
    show();
    raise();
}

void OverlayPopup::dismiss()
{
    if (!isVisible())
        return;
    parentWidget()->removeEventFilter(watcher_);
    if (parentWidget()->windowHandle())
        parentWidget()->windowHandle()->removeEventFilter(watcher_);
    hide();
    if (previousFocus_ && previousFocus_->isVisible())
        previousFocus_->setFocus();
}

void OverlayPopup::resetSize()
{
    QSettings().remove(settingsKey_);
    QSettings().remove(offsetKey());
    if (isVisible())
        place();
}

void OverlayPopup::mousePressEvent(QMouseEvent *event)
{
    const Qt::Edges edges = WindowFrame::edgesAt(event->position().toPoint(), size(), shadowWidth);
    if (event->button() != Qt::LeftButton) {
        QFrame::mousePressEvent(event);
        return;
    }
    // Inside, the press reaches the panel only where no control took it.
    resizeEdges_ = edges;
    isMoving_ = !edges;
    dragStart_ = mapToParent(event->position().toPoint());
    dragGeometry_ = geometry();
    if (isMoving_)
        setCursor(Qt::ClosedHandCursor);
}

void OverlayPopup::mouseMoveEvent(QMouseEvent *event)
{
    // Positions in the window: the panel moves while it is dragged.
    const QPoint delta = mapToParent(event->position().toPoint()) - dragStart_;
    if (isMoving_) {
        setGeometry(keptInside(dragGeometry_.translated(delta)));
        return;
    }
    if (!resizeEdges_) {
        setCursor(WindowFrame::cursorFor(WindowFrame::edgesAt(event->position().toPoint(), size(), shadowWidth)));
        QFrame::mouseMoveEvent(event);
        return;
    }
    const QRect bounds = availableArea().adjusted(-shadowWidth, -shadowWidth, shadowWidth, shadowWidth);
    const QSize smallest = minimum_ + QSize(2 * shadowWidth, 2 * shadowWidth);
    QRect rect = dragGeometry_;
    if (resizeEdges_ & Qt::LeftEdge)
        rect.setLeft(std::clamp(rect.left() + delta.x(), bounds.left(), rect.right() - smallest.width()));
    if (resizeEdges_ & Qt::RightEdge)
        rect.setRight(std::clamp(rect.right() + delta.x(), rect.left() + smallest.width(), bounds.right()));
    if (resizeEdges_ & Qt::TopEdge)
        rect.setTop(std::clamp(rect.top() + delta.y(), bounds.top(), rect.bottom() - smallest.height()));
    if (resizeEdges_ & Qt::BottomEdge)
        rect.setBottom(std::clamp(rect.bottom() + delta.y(), rect.top() + smallest.height(), bounds.bottom()));
    setGeometry(rect);
}

void OverlayPopup::mouseReleaseEvent(QMouseEvent *event)
{
    if (isMoving_) {
        isMoving_ = false;
        unsetCursor();
        saveOffset();
        return;
    }
    if (resizeEdges_) {
        resizeEdges_ = {};
        // The user's size, from now on.
        QSettings().setValue(settingsKey_, size() - QSize(2 * shadowWidth, 2 * shadowWidth));
        saveOffset();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

// The user's place, from now on: how far from the centered one. A
// transient panel goes back to the center each time.
void OverlayPopup::saveOffset()
{
    if (behavior_ == Behavior::Transient)
        return;
    QSettings().remove(offsetKey());
    const QRect centered = placedGeometry();
    QSettings().setValue(offsetKey(), geometry().topLeft() - centered.topLeft());
}

QString OverlayPopup::offsetKey() const
{
    return settingsKey_ + QStringLiteral("Offset");
}

void OverlayPopup::leaveEvent(QEvent *event)
{
    if (!resizeEdges_ && !isMoving_)
        unsetCursor();
    QFrame::leaveEvent(event);
}

void OverlayPopup::onWatchedEvent(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        place();
    else if (behavior_ == Behavior::Transient && isPressOutside(watched, event))
        dismiss();
    else if (event->type() == QEvent::MouseMove && watched == parentWidget()->windowHandle())
        dropEdgeCursorOverChild(static_cast<QMouseEvent *>(event));
}

// The panel misses the move leaving its edge for one of its controls: its
// resize cursor would stay, inherited by every control without one.
void OverlayPopup::dropEdgeCursorOverChild(const QMouseEvent *mouse)
{
    if (resizeEdges_ || isMoving_ || !testAttribute(Qt::WA_SetCursor))
        return;
    // The window's coordinates are its widget's, the panel's parent.
    if (childAt(mapFromParent(mouse->position().toPoint())))
        unsetCursor();
}

bool OverlayPopup::isPressOutside(QObject *watched, QEvent *event) const
{
    if (event->type() != QEvent::MouseButtonPress || watched != parentWidget()->windowHandle())
        return false;
    // The window's coordinates are its widget's, the panel's parent.
    const QPointF position = static_cast<QMouseEvent *>(event)->position();
    return !geometry().contains(position.toPoint());
}

void OverlayPopup::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF panel = QRectF(rect()).adjusted(shadowWidth, shadowWidth, -shadowWidth, -shadowWidth);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    // A short, discreet shadow, a little lower than the panel.
    painter.setPen(Qt::NoPen);
    for (int i = shadowSpread; i > 0; --i) {
        const qreal share = 1.0 - qreal(i) / shadowSpread;
        painter.setBrush(QColor(0, 0, 0, qRound((dark ? 48 : 24) * share * share / 3.0)));
        painter.drawRoundedRect(panel.adjusted(-i, -i + shadowDrop, i, i + shadowDrop), radius + i, radius + i);
    }
    painter.setBrush(palette().color(QPalette::AlternateBase));
    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.drawRoundedRect(panel.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}
