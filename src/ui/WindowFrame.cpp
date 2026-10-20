#include "WindowFrame.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>
#include <QWindow>

namespace {

// Room inside the window that still resizes it, for the corners.
constexpr int cornerGrip = 6;

} // namespace

WindowFrame::WindowFrame(QWidget *window)
    : QObject(window)
    , window_(window)
    , theme_(Theme::dark())
{
    window_->installEventFilter(this);
}

Qt::CursorShape WindowFrame::cursorFor(Qt::Edges edges)
{
    if (edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge))
        return Qt::SizeFDiagCursor;
    if (edges == (Qt::TopEdge | Qt::RightEdge) || edges == (Qt::BottomEdge | Qt::LeftEdge))
        return Qt::SizeBDiagCursor;
    if (edges & (Qt::LeftEdge | Qt::RightEdge))
        return Qt::SizeHorCursor;
    if (edges & (Qt::TopEdge | Qt::BottomEdge))
        return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

Qt::Edges WindowFrame::edgesAt(const QPoint &position, const QSize &size, int margin)
{
    Qt::Edges edges;
    const int grip = margin + cornerGrip;
    const bool nearLeft = position.x() < grip;
    const bool nearRight = position.x() >= size.width() - grip;
    const bool nearTop = position.y() < grip;
    const bool nearBottom = position.y() >= size.height() - grip;
    const bool inShadow = position.x() < margin || position.x() >= size.width() - margin || position.y() < margin ||
                          position.y() >= size.height() - margin;
    // In the shadow, or in a corner of the window itself.
    if (!inShadow && !((nearLeft || nearRight) && (nearTop || nearBottom)))
        return edges;
    if (nearLeft)
        edges |= Qt::LeftEdge;
    if (nearRight)
        edges |= Qt::RightEdge;
    if (nearTop)
        edges |= Qt::TopEdge;
    if (nearBottom)
        edges |= Qt::BottomEdge;
    return edges;
}

void WindowFrame::setFrameless(bool frameless)
{
    if (frameless == frameless_ && window_->testAttribute(Qt::WA_WState_Created))
        return;
    frameless_ = frameless;
    const bool visible = window_->isVisible();
    // The native window is recreated with its new flags.
    window_->setAttribute(Qt::WA_TranslucentBackground, frameless);
    window_->setWindowFlag(Qt::FramelessWindowHint, frameless);
    updateMargins();
    if (visible)
        window_->show();
}

void WindowFrame::applyTheme(const Theme &theme)
{
    theme_ = theme;
    window_->update();
}

bool WindowFrame::drawsShadow() const
{
    return frameless_ && !(window_->windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen));
}

void WindowFrame::updateMargins()
{
    const int margin = drawsShadow() ? shadowWidth : 0;
    window_->setContentsMargins(margin, margin, margin, margin);
    window_->update();
}

void WindowFrame::paint()
{
    QPainter painter(window_);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!drawsShadow()) {
        painter.fillRect(window_->rect(), theme_.window);
        return;
    }
    const QRectF content = QRectF(window_->rect()).adjusted(shadowWidth, shadowWidth, -shadowWidth, -shadowWidth);
    // The shadow: rounded rectangles fading out, a little lower than the
    // window as if lit from above.
    painter.setPen(Qt::NoPen);
    const int maxAlpha = theme_.isDark ? 70 : 38;
    for (int i = shadowWidth; i > 0; --i) {
        const qreal share = 1.0 - qreal(i) / shadowWidth;
        const QColor shadow(0, 0, 0, qRound(maxAlpha * share * share / 3.0));
        painter.setBrush(shadow);
        painter.drawRoundedRect(content.adjusted(-i, -i + 2, i, i + 2), cornerRadius + i, cornerRadius + i);
    }
    // The window itself, and its outline.
    painter.setBrush(theme_.window);
    painter.setPen(QPen(theme_.isDark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 40), 1));
    painter.drawRoundedRect(content.adjusted(0.5, 0.5, -0.5, -0.5), cornerRadius, cornerRadius);
}

bool WindowFrame::eventFilter(QObject *watched, QEvent *event)
{
    // The native window sees every move, over a child or not: the widget
    // itself would miss the one leaving the shadow for a child, and its
    // resize cursor would stay, inherited by every child.
    if (watched == window_->windowHandle() && event->type() == QEvent::MouseMove)
        updateCursor(static_cast<QMouseEvent *>(event));
    if (watched != window_)
        return false;
    switch (event->type()) {
    case QEvent::Show:
        // Each new native window (one per change of frame) is watched.
        if (window_->windowHandle())
            window_->windowHandle()->installEventFilter(this);
        break;
    case QEvent::Paint:
        if (frameless_)
            paint();
        break;
    case QEvent::WindowStateChange:
        updateMargins();
        break;
    case QEvent::StyleChange:
    case QEvent::Polish:
        // A style sheet resets the margins (at each change of theme).
        if (frameless_)
            QMetaObject::invokeMethod(this, &WindowFrame::updateMargins, Qt::QueuedConnection);
        break;
    case QEvent::MouseButtonPress:
        return startResize(static_cast<QMouseEvent *>(event));
    case QEvent::Leave:
        if (frameless_)
            window_->unsetCursor();
        break;
    default:
        break;
    }
    return false;
}

void WindowFrame::updateCursor(const QMouseEvent *mouse)
{
    if (!drawsShadow() || mouse->buttons() != Qt::NoButton)
        return;
    // The native window's coordinates are the widget's.
    const QPoint position = mouse->position().toPoint();
    const Qt::Edges edges = edgesAt(position, window_->size(), shadowWidth);
    // A child under the mouse takes the press, so it is not a resize.
    const bool isOverFrame = edges && !window_->childAt(position);
    if (isOverFrame)
        window_->setCursor(cursorFor(edges));
    else if (window_->testAttribute(Qt::WA_SetCursor))
        window_->unsetCursor();
}

bool WindowFrame::startResize(const QMouseEvent *mouse)
{
    if (!drawsShadow() || mouse->button() != Qt::LeftButton || !window_->windowHandle())
        return false;
    const Qt::Edges edges = edgesAt(mouse->position().toPoint(), window_->size(), shadowWidth);
    if (!edges)
        return false;
    window_->windowHandle()->startSystemResize(edges);
    return true;
}
