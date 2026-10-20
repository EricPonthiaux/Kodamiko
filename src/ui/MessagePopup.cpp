// SPDX-License-Identifier: GPL-3.0-only

#include "MessagePopup.h"

#include "PopupWidgets.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

MessagePopup::MessagePopup(QString iconName, const QString &settingsKey, const Theme &theme, QWidget *parent)
    : OverlayPopup(settingsKey, QSizeF(0.36, 0.26), QSize(520, 260), parent)
    , header_(new PopupHeader(std::move(iconName), QString(), this))
    , text_(new QLabel(this))
    , close_(new QPushButton(tr("Close"), this))
{
    connect(header_, &PopupHeader::closeRequested, this, &MessagePopup::next);

    // Selectable: a path or an error is copied from it.
    text_->setWordWrap(true);
    text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto *body = new QVBoxLayout;
    body->setContentsMargins(16, 14, 16, 14);
    body->addWidget(text_, 1);

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    auto *hint = new QLabel(tr("Enter or Esc to close"), footer);
    hint->setObjectName(QStringLiteral("footerHint"));
    close_->setParent(footer);
    close_->setDefault(true);
    close_->setMinimumWidth(84);
    connect(close_, &QPushButton::clicked, this, &MessagePopup::next);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 10, 16, 10);
    footerLayout->addWidget(hint, 1);
    footerLayout->addWidget(close_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addLayout(body, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

void MessagePopup::popup(const QString &title, const QString &text)
{
    load(title, text);
    if (isVisible())
        return;
    showCentered();
    close_->setFocus();
}

void MessagePopup::load(const QString &title, const QString &text)
{
    const Message message{.title = title, .text = text};
    if (!queue_.isEmpty() && queue_.last() == message)
        return;
    queue_.append(message);
    showFirst();
}

void MessagePopup::next()
{
    if (!queue_.isEmpty())
        queue_.removeFirst();
    if (queue_.isEmpty()) {
        dismiss();
        return;
    }
    showFirst();
}

void MessagePopup::showFirst()
{
    const Message &message = queue_.first();
    header_->setTitle(message.title);
    text_->setText(message.text);
    const qsizetype waiting = waitingCount();
    header_->setSubtitle(waiting > 0 ? tr("%n more message(s) after this one", nullptr, int(waiting)) : QString());
    close_->setText(waiting > 0 ? tr("Next") : tr("Close"));
}

void MessagePopup::setIconName(const QString &iconName)
{
    header_->setIconName(iconName);
}

void MessagePopup::applyTheme(const Theme &theme)
{
    setStyleSheet(popupStyleSheet(theme));
    header_->applyTheme(theme);
}

QString MessagePopup::text() const
{
    return queue_.isEmpty() ? QString() : queue_.first().text;
}

qsizetype MessagePopup::waitingCount() const
{
    return std::max<qsizetype>(queue_.size() - 1, 0);
}

void MessagePopup::keyPressEvent(QKeyEvent *event)
{
    const bool closes =
        event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (closes) {
        next();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
