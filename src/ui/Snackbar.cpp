// SPDX-License-Identifier: GPL-3.0-only

#include "Snackbar.h"

#include "AppFonts.h"
#include "Motion.h"
#include "SnackbarButton.h"
#include "ThemeStyle.h"
#include "UiIcons.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int shadowRoomPx = 12;
constexpr int shadowSpreadPx = 8;
constexpr int shadowDropPx = 3;
constexpr qreal cornerRadiusPx = 10;
constexpr qreal accentStripPx = 3;
constexpr int iconSizePx = 22;
constexpr int widestPx = 720;
constexpr int narrowestPx = 360;
constexpr int paddingPx = 16;
constexpr int gapPx = 12;
constexpr int actionGapPx = 8;
constexpr int bottomMarginPx = 48;
constexpr int sideMarginPx = 16;
constexpr int slideDistancePx = 14;

int longestLineWidth(const QFontMetrics &metrics, const QString &text)
{
    int widest = 0;
    for (const QString &line : text.split(QLatin1Char('\n')))
        widest = std::max(widest, metrics.horizontalAdvance(line));
    return widest;
}

QFont detailFont(const QWidget *widget)
{
    return AppFonts::codeFont(widget->font().pointSizeF() * 0.95);
}

} // namespace

Snackbar::Snackbar(QWidget *window)
    : QFrame(window)
    , theme_(Theme::kodamiko())
    , icon_(new QLabel(this))
    , body_(new QTextEdit(this))
    , waiting_(new QLabel(this))
    , controls_(new QWidget(this))
    , footer_(new QScrollArea(this))
    , actionContent_(new QWidget(footer_))
    , actions_(new QBoxLayout(QBoxLayout::LeftToRight, actionContent_))
    , close_(new QToolButton(controls_))
    , expiry_(new QTimer(this))
    , slide_(new QPropertyAnimation(this, "pos", this))
{
    Q_ASSERT(window);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    icon_->setFixedSize(iconSizePx, iconSizePx);
    body_->setObjectName(QStringLiteral("snackbarMessage"));
    body_->setReadOnly(true);
    body_->setFocusPolicy(Qt::TabFocus);
    body_->setFrameShape(QFrame::NoFrame);
    body_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    body_->setStyleSheet(QStringLiteral("QTextEdit { background: transparent; border: none; padding: 0; }"));
    body_->viewport()->setAutoFillBackground(false);
    body_->document()->setDocumentMargin(0);
    body_->setMinimumSize(0, 0);
    body_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    waiting_->setForegroundRole(QPalette::PlaceholderText);
    waiting_->setAlignment(Qt::AlignCenter);
    close_->setAutoRaise(true);
    close_->setFocusPolicy(Qt::TabFocus);
    close_->setFixedSize(28, 28);
    expiry_->setSingleShot(true);
    Motion::track(slide_);

    connect(close_, &QToolButton::clicked, this, [this] {
        if (queue_.isEmpty())
            return;
        const std::function<void()> dismissed = queue_.first().message.dismissed;
        closeFirst();
        if (dismissed)
            dismissed();
    });
    connect(expiry_, &QTimer::timeout, close_, &QToolButton::click);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *focused) {
        if (focused && isAncestorOf(focused))
            expiry_->stop();
        else if (!queue_.isEmpty() && !underMouse() && queue_.first().message.duration)
            expiry_->start(*queue_.first().message.duration);
    });

    auto *controls = new QVBoxLayout(controls_);
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(4);
    controls->addWidget(close_);
    controls->addWidget(waiting_);
    auto *message = new QHBoxLayout;
    message->setSpacing(gapPx);
    message->addWidget(icon_, 0, Qt::AlignTop);
    message->addWidget(body_, 1);
    message->addWidget(controls_, 0, Qt::AlignTop);

    footer_->setObjectName(QStringLiteral("snackbarActions"));
    footer_->setFrameShape(QFrame::NoFrame);
    footer_->setFocusPolicy(Qt::NoFocus);
    footer_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    footer_->setMinimumSize(0, 0);
    footer_->setWidget(actionContent_);
    footer_->viewport()->setAutoFillBackground(false);
    actionContent_->setAutoFillBackground(false);
    actions_->setContentsMargins(0, 0, 0, 0);
    actions_->setSpacing(actionGapPx);
    actions_->setSizeConstraint(QLayout::SetNoConstraint);
    actions_->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->setContentsMargins(shadowRoomPx + paddingPx, shadowRoomPx + paddingPx, shadowRoomPx + paddingPx,
                               shadowRoomPx + paddingPx);
    layout->setSpacing(gapPx);
    layout->addLayout(message, 1);
    layout->addWidget(footer_);

    window->installEventFilter(this);
    applyTheme(theme_);
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
        updateWaiting();
        place();
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
    updateWaiting();
    place();
}

QString Snackbar::shownTitle() const
{
    return queue_.isEmpty() ? QString() : queue_.first().message.title;
}

void Snackbar::applyTheme(const Theme &theme)
{
    theme_ = theme;
    close_->setIcon(UiIcons::icon(QStringLiteral("close"), theme.mutedText));
    if (!queue_.isEmpty()) {
        updateText();
        place();
    }
    update();
}

bool Snackbar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parent() && event->type() == QEvent::Resize && isVisible())
        place();
    return QFrame::eventFilter(watched, event);
}

bool Snackbar::event(QEvent *event)
{
    const bool handled = QFrame::event(event);
    if (queue_.isEmpty() || placing_)
        return handled;
    if (event->type() == QEvent::FontChange)
        updateText();
    if (event->type() == QEvent::LayoutRequest || event->type() == QEvent::FontChange)
        place();
    return handled;
}

void Snackbar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(shadowRoomPx, shadowRoomPx, -shadowRoomPx, -shadowRoomPx);
    painter.setPen(Qt::NoPen);
    const int shadowAlpha = theme_.isDark ? 70 : 34;
    for (int i = shadowSpreadPx; i > 0; --i) {
        const qreal share = 1.0 - qreal(i) / shadowSpreadPx;
        painter.setBrush(QColor(0, 0, 0, qRound(shadowAlpha * share * share / 3.0)));
        painter.drawRoundedRect(card.adjusted(-i, -i + shadowDropPx, i, i + shadowDropPx), cornerRadiusPx + i,
                                cornerRadiusPx + i);
    }
    QPainterPath shape;
    shape.addRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), cornerRadiusPx, cornerRadiusPx);
    painter.fillPath(shape, ThemeStyle::mixed(theme_.window, theme_.foreground, theme_.isDark ? 0.08 : 0.02));
    painter.save();
    painter.setClipPath(shape);
    painter.fillRect(QRectF(card.left(), card.top(), accentStripPx, card.height()), theme_.accent);
    painter.restore();
    painter.setPen(QPen(theme_.border, 1));
    painter.drawPath(shape);
    if (!footer_->isHidden()) {
        const int dividerY = footer_->y() - gapPx / 2;
        painter.setPen(QPen(ThemeStyle::mixed(theme_.window, theme_.border, 0.65), 1));
        painter.drawLine(footer_->x(), dividerY, footer_->geometry().right(), dividerY);
    }
}

void Snackbar::enterEvent(QEnterEvent *event)
{
    expiry_->stop();
    QFrame::enterEvent(event);
}

void Snackbar::leaveEvent(QEvent *event)
{
    if (!queue_.isEmpty() && !isAncestorOf(QApplication::focusWidget())) {
        if (const auto duration = queue_.first().message.duration)
            expiry_->start(*duration);
    }
    QFrame::leaveEvent(event);
}

void Snackbar::updateText()
{
    const SnackbarMessage &message = queue_.first().message;
    const int scroll = body_->verticalScrollBar()->value();
    body_->clear();
    QTextCursor cursor(body_->document());
    QTextCharFormat titleFormat;
    QFont titleFont = font();
    titleFont.setWeight(QFont::DemiBold);
    titleFormat.setFont(titleFont);
    titleFormat.setForeground(theme_.foreground);
    cursor.insertText(message.title, titleFormat);
    if (!message.detail.isEmpty()) {
        QTextBlockFormat block;
        block.setBottomMargin(gapPx);
        cursor.setBlockFormat(block);
        cursor.insertBlock(QTextBlockFormat());
        QTextCharFormat codeFormat;
        codeFormat.setFont(detailFont(this));
        codeFormat.setForeground(theme_.mutedText);
        cursor.insertText(message.detail, codeFormat);
    }
    body_->moveCursor(QTextCursor::Start);
    body_->setAccessibleName(message.title);
    body_->verticalScrollBar()->setValue(scroll);
}

void Snackbar::updateWaiting()
{
    waiting_->setText(tr("+%1").arg(queue_.size() - 1));
    waiting_->setToolTip(tr("%n more message(s)", nullptr, int(queue_.size() - 1)));
    waiting_->setVisible(queue_.size() > 1);
}

void Snackbar::showFirst()
{
    const SnackbarMessage &message = queue_.first().message;
    icon_->setPixmap(message.icon.pixmap(QSize(iconSizePx, iconSizePx), devicePixelRatioF()));
    icon_->setVisible(!message.icon.isNull());
    updateText();
    body_->verticalScrollBar()->setValue(0);
    close_->setToolTip(message.closeToolTip.isEmpty() ? tr("Close") : message.closeToolTip);
    close_->setAccessibleName(close_->toolTip());
    updateWaiting();
    rebuildActions();
    if (message.duration && !underMouse() && !isAncestorOf(QApplication::focusWidget()))
        expiry_->start(*message.duration);
    else
        expiry_->stop();
    ensurePolished();
    place();
    show();
    raise();
}

void Snackbar::closeFirst()
{
    if (queue_.isEmpty())
        return;
    queue_.removeFirst();
    expiry_->stop();
    slide_->stop();
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
        actions_->removeWidget(button);
        button->hide();
        button->deleteLater();
    }
    actionButtons_.clear();
    const Id id = queue_.first().id;
    for (const SnackbarAction &action : queue_.first().message.actions) {
        auto *button = new SnackbarButton(action.text, actionContent_);
        button->setDefault(action.emphasis == SnackbarAction::Emphasis::Primary);
        connect(button, &QPushButton::clicked, this, [this, id, run = action.run] {
            withdraw(id);
            if (run)
                run();
        });
        actions_->addWidget(button);
        actionButtons_.append(button);
        button->show();
    }
    footer_->setVisible(!actionButtons_.isEmpty());
    footer_->verticalScrollBar()->setValue(0);
}

int Snackbar::arrangeActions(int width)
{
    if (actionButtons_.isEmpty())
        return 0;
    int naturalWidth = (int(actionButtons_.size()) - 1) * actionGapPx;
    for (const QPushButton *button : actionButtons_)
        naturalWidth += button->sizeHint().width();
    const bool stacked = naturalWidth > width;
    actions_->setDirection(stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    int height = 0;
    for (QPushButton *button : actionButtons_) {
        const int buttonWidth = stacked ? width : button->sizeHint().width();
        const int buttonHeight = button->heightForWidth(buttonWidth);
        button->setFixedSize(buttonWidth, buttonHeight);
        height = stacked ? height + buttonHeight : std::max(height, buttonHeight);
    }
    if (stacked)
        height += (int(actionButtons_.size()) - 1) * actionGapPx;
    actionContent_->setFixedSize(width, height);
    actions_->activate();
    return height;
}

void Snackbar::place()
{
    if (queue_.isEmpty() || placing_)
        return;
    const QScopedValueRollback guard(placing_, true);
    slide_->stop();
    const QWidget *window = parentWidget();
    const QMargins margins = layout()->contentsMargins();
    const int horizontalChrome = margins.left() + margins.right();
    const int verticalChrome = margins.top() + margins.bottom();
    const int availableWidth = std::max(1, window->width() - 2 * sideMarginPx);
    const int controlWidth = controls_->sizeHint().width() + gapPx;
    const int iconWidth = icon_->isHidden() ? 0 : iconSizePx + gapPx;
    const auto &message = queue_.first().message;
    const int textWidth = std::max(longestLineWidth(body_->fontMetrics(), message.title),
                                   longestLineWidth(QFontMetrics(detailFont(this)), message.detail));
    int actionWidth = 0;
    for (const QPushButton *button : actionButtons_)
        actionWidth += button->sizeHint().width() + actionGapPx;
    const int naturalWidth = horizontalChrome + std::max(textWidth + controlWidth + iconWidth, actionWidth);
    const int width =
        std::clamp(naturalWidth, std::min(narrowestPx, availableWidth), std::min(widestPx, availableWidth));
    const int innerWidth = std::max(1, width - horizontalChrome);
    const int scrollbarWidth = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    const int bodyWidth = std::max(1, innerWidth - controlWidth - iconWidth - scrollbarWidth);
    body_->document()->setTextWidth(bodyWidth);
    const int textHeight = int(std::ceil(body_->document()->size().height())) + 2;
    const int controlsHeight = controls_->sizeHint().height();
    const int messageHeight = std::max({textHeight, controlsHeight, iconSizePx});
    const int bottom = std::min(bottomMarginPx, window->height() / 10);
    const int availableHeight = std::max(1, window->height() - bottom - sideMarginPx);
    const int footerGap = actionButtons_.isEmpty() ? 0 : gapPx;
    const int maxActionHeight = std::max(1, availableHeight - verticalChrome - controlsHeight - footerGap);
    int actionHeight = arrangeActions(innerWidth);
    if (actionHeight > maxActionHeight)
        actionHeight = arrangeActions(std::max(1, innerWidth - scrollbarWidth));
    const int footerHeight = std::min(actionHeight, maxActionHeight);
    footer_->setFixedHeight(footerHeight);
    const int fixedHeight = verticalChrome + footerHeight + footerGap;
    // Use at most 60% of a normal window; short windows give priority to the
    // controls and at least a few readable lines before scrolling the text.
    const int preferredHeight = std::max(window->height() * 3 / 5, fixedHeight + controlsHeight);
    const int height = std::min({fixedHeight + messageHeight, preferredHeight, availableHeight});
    setGeometry((window->width() - width) / 2, window->height() - bottom - height, width, height);
    layout()->activate();
    update();
}

void Snackbar::slideIn()
{
    if (!Motion::policy().isEnabled())
        return;
    Motion::fadeIn(this);
    const QPoint end = pos();
    slide_->setStartValue(end + QPoint(0, slideDistancePx));
    slide_->setEndValue(end);
    slide_->setDuration(int(Motion::enterDuration.count()));
    slide_->setEasingCurve(Motion::enterCurve());
    slide_->start();
}
