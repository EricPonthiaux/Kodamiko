// SPDX-License-Identifier: GPL-3.0-only

#include "ChoicePopup.h"

#include "ui/Paragraph.h"

#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ChoicePopup::ChoicePopup(const Question &question, const QList<Choice> &choices, QWidget *parent)
    : EntryPopup(QStringLiteral("choicePopup"), question.title, question.location, parent)
    , choices_(choices)
    , cancel_(new QPushButton(choices.isEmpty() ? tr("Close") : tr("Cancel"), this))
{
    auto *message = new QLabel(this);
    Paragraph::setText(message, question.message);
    // Selectable: an error or a path is copied from it.
    message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    content()->addWidget(message);
    if (!question.details.isEmpty()) {
        auto *details = new QLabel(this);
        Paragraph::setText(details, question.details);
        details->setTextInteractionFlags(Qt::TextSelectableByMouse);
        details->setObjectName(QStringLiteral("settingsDescription"));
        content()->addWidget(details);
    }

    setIconName(QStringLiteral("problems"));
    setFooterHint(choices_.isEmpty() ? tr("Enter or Esc to close") : tr("Enter to choose · Esc to cancel"));
    connect(cancel_, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    addFooterButton(cancel_);
    for (qsizetype i = 0; i < choices_.size(); ++i) {
        auto *button = new QPushButton(choices_[i].text, this);
        connect(button, &QPushButton::clicked, this, [this, i] { choose(i); });
        addFooterButton(button);
        buttons_.append(button);
    }
}

void ChoicePopup::setCancelText(const QString &text)
{
    cancel_->setText(text);
}

void ChoicePopup::focusCancel()
{
    focusesCancel_ = true;
    if (isVisible())
        cancel_->setFocus();
}

void ChoicePopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        accept();
        return;
    }
    EntryPopup::keyPressEvent(event);
}

void ChoicePopup::accept()
{
    const qsizetype index = buttons_.indexOf(qobject_cast<QPushButton *>(focusWidget()));
    if (index < 0)
        dismiss();
    else
        choose(index);
}

void ChoicePopup::focusContent()
{
    if (focusesCancel_ || buttons_.isEmpty())
        cancel_->setFocus();
    else
        buttons_.first()->setFocus();
}

void ChoicePopup::choose(qsizetype index)
{
    // Dismissed first: the choice may ask something else.
    const std::function<void()> run = choices_[index].run;
    dismiss();
    run();
}

void showNotice(const ChoicePopup::Question &question, QWidget *parent)
{
    auto *popup = new ChoicePopup(question, {}, parent);
    popup->popup();
}
