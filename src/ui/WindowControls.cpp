#include "WindowControls.h"

#include "UiMetrics.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>

namespace {

constexpr int controlWidth = 46;

} // namespace

WindowButton::WindowButton(Kind kind, QWidget *parent)
    : QAbstractButton(parent)
    , kind_(kind)
    , theme_(Theme::dark())
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
}

QSize WindowButton::sizeHint() const
{
    return {controlWidth, UiMetrics::scaled(UiMetrics::titleBarHeight)};
}

void WindowButton::applyTheme(const Theme &theme)
{
    theme_ = theme;
    update();
}

void WindowButton::enterEvent(QEnterEvent *event)
{
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void WindowButton::leaveEvent(QEvent *event)
{
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void WindowButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor color = theme_.foreground;
    if (hovered_ || isDown()) {
        // Closing turns red, as everywhere.
        const QColor background =
            kind_ == Kind::Close ? QColor("#e81123") : theme_.hover.lighter(theme_.isDark ? 140 : 97);
        painter.fillRect(rect(), isDown() ? background.darker(115) : background);
        if (kind_ == Kind::Close)
            color = Qt::white;
    }
    painter.setPen(QPen(color, 1.1));
    painter.setBrush(Qt::NoBrush);
    const QPointF center = QRectF(rect()).center();
    const qreal half = 5;
    switch (kind_) {
    case Kind::Minimize:
        painter.drawLine(QPointF(center.x() - half, center.y()), QPointF(center.x() + half, center.y()));
        break;
    case Kind::Maximize:
        if (window()->isMaximized()) {
            // Restore: two overlapping squares.
            painter.drawRoundedRect(QRectF(center.x() - half, center.y() - half + 2, 2 * half - 2, 2 * half - 2), 1.5,
                                    1.5);
            QPainterPath back;
            back.moveTo(center.x() - half + 2, center.y() - half + 2);
            back.lineTo(center.x() - half + 2, center.y() - half);
            back.lineTo(center.x() + half, center.y() - half);
            back.lineTo(center.x() + half, center.y() + half - 2);
            back.lineTo(center.x() + half - 2, center.y() + half - 2);
            painter.drawPath(back);
        } else {
            painter.drawRoundedRect(QRectF(center.x() - half, center.y() - half, 2 * half, 2 * half), 1.5, 1.5);
        }
        break;
    case Kind::Close:
        painter.drawLine(QPointF(center.x() - half, center.y() - half), QPointF(center.x() + half, center.y() + half));
        painter.drawLine(QPointF(center.x() + half, center.y() - half), QPointF(center.x() - half, center.y() + half));
        break;
    }
}

WindowControls::WindowControls(QWidget *parent)
    : QWidget(parent)
    , window_(parent->window())
    , minimize_(new WindowButton(WindowButton::Kind::Minimize, this))
    , maximize_(new WindowButton(WindowButton::Kind::Maximize, this))
    , close_(new WindowButton(WindowButton::Kind::Close, this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    minimize_->setToolTip(tr("Minimize"));
    close_->setToolTip(tr("Close"));
    for (WindowButton *button : {minimize_, maximize_, close_})
        layout->addWidget(button);
    connect(minimize_, &QAbstractButton::clicked, window_, &QWidget::showMinimized);
    connect(maximize_, &QAbstractButton::clicked, this, [this] { toggleMaximized(window_); });
    connect(close_, &QAbstractButton::clicked, window_, &QWidget::close);
    updateMaximizeButton();
    window_->installEventFilter(this); // maximized or not
}

WindowButton *WindowControls::button(WindowButton::Kind kind) const
{
    switch (kind) {
    case WindowButton::Kind::Minimize:
        return minimize_;
    case WindowButton::Kind::Maximize:
        return maximize_;
    case WindowButton::Kind::Close:
        break;
    }
    return close_;
}

void WindowControls::applyTheme(const Theme &theme)
{
    for (WindowButton *button : {minimize_, maximize_, close_})
        button->applyTheme(theme);
}

void WindowControls::toggleMaximized(QWidget *window)
{
    if (window->isMaximized())
        window->showNormal();
    else
        window->showMaximized();
}

void WindowControls::updateMaximizeButton()
{
    maximize_->setToolTip(window_->isMaximized() ? tr("Restore") : tr("Maximize"));
    maximize_->update();
}

bool WindowControls::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == window_ && event->type() == QEvent::WindowStateChange)
        updateMaximizeButton();
    return QWidget::eventFilter(watched, event);
}
