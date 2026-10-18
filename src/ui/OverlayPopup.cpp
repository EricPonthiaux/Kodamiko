#include "OverlayPopup.h"

#include "WindowFrame.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>

namespace {

constexpr int radius = 12;
constexpr int windowMargin = 24; // between the panel and the window's edges

} // namespace

class OverlayPopup::Watcher : public QObject {
public:
    explicit Watcher(OverlayPopup *popup)
        : QObject(popup)
        , popup_(popup)
    {
    }

protected:
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

void OverlayPopup::place()
{
    const QRect area = availableArea();
    const QSize size = panelSize();
    // Centered across, a little above the middle (the optical center).
    const int x = area.left() + (area.width() - size.width()) / 2;
    const int y = area.top() + std::max((area.height() - size.height()) / 3, 0);
    setGeometry(QRect(QPoint(x, y), size).adjusted(-shadowWidth, -shadowWidth, shadowWidth, shadowWidth));
}

void OverlayPopup::showCentered()
{
    if (!isVisible()) {
        previousFocus_ = QApplication::focusWidget();
        applyMargins();
        place();
        parentWidget()->installEventFilter(watcher_); // window resized
        qApp->installEventFilter(watcher_);           // clicks elsewhere
    }
    show();
    raise();
}

void OverlayPopup::dismiss()
{
    if (!isVisible())
        return;
    qApp->removeEventFilter(watcher_);
    parentWidget()->removeEventFilter(watcher_);
    hide();
    if (previousFocus_ && previousFocus_->isVisible())
        previousFocus_->setFocus();
}

void OverlayPopup::resetSize()
{
    QSettings().remove(settingsKey_);
    if (isVisible())
        place();
}

void OverlayPopup::mousePressEvent(QMouseEvent *event)
{
    const Qt::Edges edges = WindowFrame::edgesAt(event->position().toPoint(), size(), shadowWidth);
    if (event->button() == Qt::LeftButton && edges) {
        resizeEdges_ = edges;
        resizeStart_ = mapToParent(event->position().toPoint());
        resizeGeometry_ = geometry();
        return;
    }
    QFrame::mousePressEvent(event);
}

void OverlayPopup::mouseMoveEvent(QMouseEvent *event)
{
    if (!resizeEdges_) {
        setCursor(WindowFrame::cursorFor(WindowFrame::edgesAt(event->position().toPoint(), size(), shadowWidth)));
        QFrame::mouseMoveEvent(event);
        return;
    }
    // Positions in the window: the panel moves while it is resized.
    const QPoint delta = mapToParent(event->position().toPoint()) - resizeStart_;
    const QRect bounds = availableArea().adjusted(-shadowWidth, -shadowWidth, shadowWidth, shadowWidth);
    const QSize smallest = minimum_ + QSize(2 * shadowWidth, 2 * shadowWidth);
    QRect rect = resizeGeometry_;
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
    if (resizeEdges_) {
        resizeEdges_ = {};
        // The user's size, from now on.
        QSettings().setValue(settingsKey_, size() - QSize(2 * shadowWidth, 2 * shadowWidth));
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

void OverlayPopup::leaveEvent(QEvent *event)
{
    if (!resizeEdges_)
        unsetCursor();
    QFrame::leaveEvent(event);
}

void OverlayPopup::onWatchedEvent(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        place();
    } else if (event->type() == QEvent::MouseButtonPress && watched->isWidgetType()) {
        // A click in the window, outside the panel and its edges: closes it.
        // Menus and other windows do not count.
        auto *widget = static_cast<QWidget *>(watched);
        if (widget->window() == parentWidget()->window() && widget != this && !isAncestorOf(widget))
            dismiss();
    }
}

void OverlayPopup::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF panel = QRectF(rect()).adjusted(shadowWidth, shadowWidth, -shadowWidth, -shadowWidth);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    // The shadow, a little lower than the panel.
    painter.setPen(Qt::NoPen);
    for (int i = shadowWidth; i > 0; --i) {
        const qreal share = 1.0 - qreal(i) / shadowWidth;
        painter.setBrush(QColor(0, 0, 0, qRound((dark ? 60 : 32) * share * share / 3.0)));
        painter.drawRoundedRect(panel.adjusted(-i, -i + 3, i, i + 3), radius + i, radius + i);
    }
    painter.setBrush(palette().color(QPalette::AlternateBase));
    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.drawRoundedRect(panel.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}
