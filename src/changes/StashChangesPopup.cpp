// SPDX-License-Identifier: GPL-3.0-only

#include "StashChangesPopup.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

StashChangesPopup::StashChangesPopup(const QString &location,
                                     std::function<void(const QString &, bool)> stash,
                                     QWidget *parent)
    : EntryPopup(QStringLiteral("stashChanges"), tr("Stash Changes"), location, parent)
    , stash_(std::move(stash))
    , message_(new QLineEdit(this))
    , untracked_(new QCheckBox(tr("Include new files"), this))
{
    auto *explanation = new QLabel(tr("Put your changes aside: the files go back to the last commit, and the "
                                      "changes can be brought back later, on this branch or another."),
                                   this);
    explanation->setWordWrap(true);
    message_->setPlaceholderText(tr("Message (optional)"));
    connect(message_, &QLineEdit::returnPressed, this, &StashChangesPopup::accept);
    untracked_->setChecked(true);

    auto *cancel = new QPushButton(tr("Cancel"), this);
    auto *stashButton = new QPushButton(tr("Stash"), this);
    stashButton->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    connect(stashButton, &QPushButton::clicked, this, &StashChangesPopup::accept);
    setIconName(QStringLiteral("branch"));
    setFooterHint(tr("Enter to stash · Esc to cancel"));
    addFooterButton(cancel);
    addFooterButton(stashButton);

    content()->addWidget(explanation);
    content()->addWidget(message_);
    content()->addWidget(untracked_);
}

void StashChangesPopup::accept()
{
    // Copied first: once dismissed, the popup goes.
    const auto stash = stash_;
    const QString message = message_->text().trimmed();
    const bool includeUntracked = untracked_->isChecked();
    dismiss();
    stash(message, includeUntracked);
}

void StashChangesPopup::focusContent()
{
    message_->setFocus();
}
