// SPDX-License-Identifier: GPL-3.0-only

#include "WindowControls.h"

#include "ThemeStyle.h"
#include "UiMetrics.h"
#include "WindowFrame.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace {

constexpr int controlWidth = 46;
// The glyphs' half side, at most: a title bar's 10 px glyph.
constexpr qreal maxGlyphHalfPx = 5;
// Below a title bar's height, as among the content's buttons, the glyph
// shrinks with the button: 5 px for a 40 px high bar.
constexpr qreal heightPerGlyphHalf = 8;
// As the menu items' (ThemeStyle).
constexpr qreal roundedRadiusPx = 4;

} // namespace

WindowButton::WindowButton(Kind kind, QWidget *parent)
    : QAbstractButton(parent)
    , kind_(kind)
    , theme_(Theme::kodamiko())
{
    setFocusPolicy(Qt::NoFocus);
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

void WindowButton::setCorners(Corners corners)
{
    corners_ = corners;
    update();
}

void WindowButton::enterEvent(QEnterEvent *event)
{
    hover_.setHovered(true);
    QAbstractButton::enterEvent(event);
}

void WindowButton::leaveEvent(QEvent *event)
{
    hover_.setHovered(false);
    QAbstractButton::leaveEvent(event);
}

void WindowButton::fillBackground(QPainter &painter, const QColor &background) const
{
    switch (corners_) {
    case Corners::Square: {
        // Against the window's edge, the window's rounded corner cuts it.
        const QPainterPath windowShape = WindowFrame::roundedContent(this->window());
        QPainterPath fill;
        fill.addRect(QRectF(rect()));
        if (!windowShape.isEmpty())
            fill = fill.intersected(windowShape.translated(-mapTo(this->window(), QPointF(0, 0))));
        painter.fillPath(fill, background);
        return;
    }
    case Corners::Rounded:
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(QRectF(rect()), roundedRadiusPx, roundedRadiusPx);
        return;
    }
}

void WindowButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor color = theme_.foreground;
    const qreal highlight = isDown() ? 1.0 : hover_.progress();
    if (highlight > 0) {
        // Closing turns red, as everywhere.
        QColor background = kind_ == Kind::Close ? QColor("#e81123") : theme_.hover.lighter(theme_.isDark ? 140 : 97);
        if (isDown())
            background = background.darker(115);
        background.setAlphaF(highlight);
        fillBackground(painter, background);
        if (kind_ == Kind::Close)
            color = ThemeStyle::mixed(color, Qt::white, highlight);
    }
    painter.setPen(QPen(color, 1.1));
    painter.setBrush(Qt::NoBrush);
    const QPointF center = QRectF(rect()).center();
    const qreal half = std::min(maxGlyphHalfPx, height() / heightPerGlyphHalf);
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
