// SPDX-License-Identifier: GPL-3.0-only

#include "Snackbar.h"

#include "AppFonts.h"
#include "Motion.h"
#include "ThemeStyle.h"
#include "UiIcons.h"

#include <QEnterEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Room around the card for its shadow.
constexpr int shadowRoomPx = 12;
constexpr int shadowSpreadPx = 8;
constexpr int shadowDropPx = 3;
constexpr qreal cornerRadiusPx = 10;
// The strip on its left edge, in the accent color.
constexpr qreal accentStripPx = 3;
constexpr int iconSizePx = 20;
constexpr int widestPx = 600;
constexpr int narrowestPx = 360;
// Above the window's bottom edge, clear of the status bar.
constexpr int bottomMarginPx = 36;
constexpr int sideMarginPx = 16;
// It rises this far as it appears.
constexpr int slideDistancePx = 14;

} // namespace

Snackbar::Snackbar(QWidget *window)
    : QFrame(window)
    , theme_(Theme::duet())
    , icon_(new QLabel(this))
    , title_(new QLabel(this))
    , detail_(new QLabel(this))
    , waiting_(new QLabel(this))
    , actions_(new QHBoxLayout)
    , close_(new QToolButton(this))
    , expiry_(new QTimer(this))
{
    Q_ASSERT(window);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    setContentsMargins(shadowRoomPx, shadowRoomPx, shadowRoomPx, shadowRoomPx);

    icon_->setFixedSize(iconSizePx, iconSizePx);
    AppFonts::setInterfaceFont(title_, 1.0);
    title_->setTextFormat(Qt::PlainText);
    detail_->setTextFormat(Qt::PlainText);
    // They take the room the buttons leave, and are elided to it (fitTexts()).
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    detail_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    detail_->setForegroundRole(QPalette::PlaceholderText);
    waiting_->setForegroundRole(QPalette::PlaceholderText);
    close_->setAutoRaise(true);
    close_->setFocusPolicy(Qt::TabFocus);
    expiry_->setSingleShot(true);

    connect(close_, &QToolButton::clicked, this, [this] {
        if (queue_.isEmpty())
            return;
        const std::function<void()> dismissed = queue_.first().message.dismissed;
        closeFirst();
        if (dismissed)
            dismissed();
    });
    connect(expiry_, &QTimer::timeout, close_, &QToolButton::click);

    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(title_);
    text->addWidget(detail_);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(int(accentStripPx) + 14, 10, 8, 10);
    layout->setSpacing(12);
    layout->addWidget(icon_, 0, Qt::AlignVCenter);
    layout->addLayout(text, 1);
    layout->addWidget(waiting_, 0, Qt::AlignVCenter);
    actions_->setSpacing(6);
    layout->addLayout(actions_);
    layout->addWidget(close_, 0, Qt::AlignVCenter);

    window->installEventFilter(this);
    // The labels learn their room once laid out, which their own resize says.
    title_->installEventFilter(this);
    detail_->installEventFilter(this);
    hide();
}

Snackbar::Id Snackbar::post(SnackbarMessage message)
{
    const Id id = ++lastId_;
    queue_.append({id, std::move(message)});
    if (queue_.size() == 1) {
        showFirst();
        slideIn();
    } else {
        waiting_->setText(tr("+%1").arg(queue_.size() - 1));
        waiting_->show();
    }
    return id;
}

void Snackbar::withdraw(Id id)
{
    const auto entry = std::ranges::find(queue_, id, &Entry::id);
    if (entry == queue_.end())
        return;
    if (entry == queue_.begin()) {
        closeFirst();
        return;
    }
    queue_.erase(entry);
    waiting_->setText(tr("+%1").arg(queue_.size() - 1));
    waiting_->setVisible(queue_.size() > 1);
}

QString Snackbar::shownTitle() const
{
    return queue_.isEmpty() ? QString() : queue_.first().message.title;
}

void Snackbar::applyTheme(const Theme &theme)
{
    theme_ = theme;
    close_->setIcon(UiIcons::icon(QStringLiteral("close"), theme.mutedText));
    detail_->setFont(AppFonts::codeFont(font().pointSizeF() * 0.95));
    if (!queue_.isEmpty())
        showFirst();
    update();
}

bool Snackbar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::Resize)
        return QFrame::eventFilter(watched, event);
    if (watched == parent() && isVisible())
        place();
    else if (watched == title_ || watched == detail_)
        fitTexts();
    return QFrame::eventFilter(watched, event);
}

void Snackbar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(shadowRoomPx, shadowRoomPx, -shadowRoomPx, -shadowRoomPx);

    // A soft shadow, a little lower than the card: it floats over the text.
    painter.setPen(Qt::NoPen);
    const int shadowAlpha = theme_.isDark ? 70 : 34;
    for (int i = shadowSpreadPx; i > 0; --i) {
        const qreal share = 1.0 - qreal(i) / shadowSpreadPx;
        painter.setBrush(QColor(0, 0, 0, qRound(shadowAlpha * share * share / 3.0)));
        painter.drawRoundedRect(card.adjusted(-i, -i + shadowDropPx, i, i + shadowDropPx), cornerRadiusPx + i,
                                cornerRadiusPx + i);
    }

    // Raised above the window's panels, so that it reads as a layer of its own.
    QPainterPath shape;
    shape.addRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), cornerRadiusPx, cornerRadiusPx);
    painter.fillPath(shape, ThemeStyle::mixed(theme_.window, theme_.foreground, theme_.isDark ? 0.08 : 0.02));
    painter.save();
    painter.setClipPath(shape);
    painter.fillRect(QRectF(card.left(), card.top(), accentStripPx, card.height()), theme_.accent);
    painter.restore();
    painter.setPen(QPen(theme_.border, 1));
    painter.drawPath(shape);
}

void Snackbar::enterEvent(QEnterEvent *event)
{
    // A message does not go away while the user reads it.
    expiry_->stop();
    QFrame::enterEvent(event);
}

void Snackbar::leaveEvent(QEvent *event)
{
    if (!queue_.isEmpty()) {
        if (const std::optional<std::chrono::milliseconds> duration = queue_.first().message.duration)
            expiry_->start(*duration);
    }
    QFrame::leaveEvent(event);
}

void Snackbar::showFirst()
{
    const SnackbarMessage &message = queue_.first().message;
    icon_->setPixmap(message.icon.pixmap(QSize(iconSizePx, iconSizePx), devicePixelRatioF()));
    icon_->setVisible(!message.icon.isNull());
    detail_->setToolTip(message.detail);
    detail_->setVisible(!message.detail.isEmpty());
    close_->setToolTip(message.closeToolTip.isEmpty() ? tr("Close") : message.closeToolTip);
    waiting_->setText(tr("+%1").arg(queue_.size() - 1));
    waiting_->setVisible(queue_.size() > 1);
    rebuildActions();
    if (message.duration)
        expiry_->start(*message.duration);
    else
        expiry_->stop();
    ensurePolished();
    place();
    layout()->activate();
    fitTexts();
    show();
    raise();
}

void Snackbar::fitTexts()
{
    if (queue_.isEmpty())
        return;
    // The style sheet sets the labels' fonts as they are polished.
    ensurePolished();
    const SnackbarMessage &message = queue_.first().message;
    title_->setText(title_->fontMetrics().elidedText(message.title, Qt::ElideRight, title_->width()));
    detail_->setText(detail_->fontMetrics().elidedText(message.detail, Qt::ElideRight, detail_->width()));
}

void Snackbar::closeFirst()
{
    if (queue_.isEmpty())
        return;
    queue_.removeFirst();
    expiry_->stop();
    if (!queue_.isEmpty()) {
        showFirst();
        return;
    }
    Motion::fadeOut(this);
    hide();
}

void Snackbar::rebuildActions()
{
    // A button may be the one being clicked: it goes once its signal returns.
    for (QPushButton *button : actionButtons_) {
        button->hide();
        button->deleteLater();
    }
    actionButtons_.clear();
    const Id id = queue_.first().id;
    for (const SnackbarAction &action : queue_.first().message.actions) {
        auto *button = new QPushButton(action.text, this);
        // A click leaves the focus where the user types.
        button->setFocusPolicy(Qt::TabFocus);
        button->setDefault(action.emphasis == SnackbarAction::Emphasis::Primary);
        button->setAutoDefault(false);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(button, &QPushButton::clicked, this, [this, id, run = action.run] {
            // The action may post or withdraw messages: close this one first.
            withdraw(id);
            if (run)
                run();
        });
        actions_->addWidget(button);
        actionButtons_.append(button);
    }
}

void Snackbar::place()
{
    if (!queue_.isEmpty())
        setGeometry(placedGeometry());
}

QRect Snackbar::placedGeometry() const
{
    const QWidget *window = parentWidget();
    const int available = window->width() - 2 * sideMarginPx;
    // The texts' room is not in the size hint: they are elided to what is left.
    const SnackbarMessage &message = queue_.first().message;
    const int textWidth = std::max(title_->fontMetrics().horizontalAdvance(message.title),
                                   detail_->fontMetrics().horizontalAdvance(message.detail));
    const int cardWidth = std::clamp(sizeHint().width() - 2 * shadowRoomPx + textWidth,
                                     std::min(narrowestPx, available), std::min(widestPx, available));
    const int cardHeight = heightForWidth(cardWidth + 2 * shadowRoomPx) > 0
                               ? heightForWidth(cardWidth + 2 * shadowRoomPx) - 2 * shadowRoomPx
                               : sizeHint().height() - 2 * shadowRoomPx;
    const QRect card((window->width() - cardWidth) / 2, window->height() - bottomMarginPx - cardHeight, cardWidth,
                     cardHeight);
    return card.adjusted(-shadowRoomPx, -shadowRoomPx, shadowRoomPx, shadowRoomPx);
}

void Snackbar::slideIn()
{
    if (!Motion::policy().isEnabled())
        return;
    Motion::fadeIn(this);
    const QPoint end = placedGeometry().topLeft();
    auto *animation = new QPropertyAnimation(this, "pos", this);
    animation->setStartValue(end + QPoint(0, slideDistancePx));
    animation->setEndValue(end);
    animation->setDuration(int(Motion::enterDuration.count()));
    animation->setEasingCurve(Motion::enterCurve());
    Motion::track(animation);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}
