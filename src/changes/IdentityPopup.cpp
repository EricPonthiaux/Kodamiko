// SPDX-License-Identifier: GPL-3.0-only

#include "IdentityPopup.h"

#include "ui/Paragraph.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

IdentityPopup::IdentityPopup(const QString &location,
                             const GitOperations::Identity &current,
                             std::function<void(const GitOperations::Identity &, GitOperations::ConfigScope)> save,
                             QWidget *parent)
    : EntryPopup(QStringLiteral("gitIdentity"), tr("Who Are You?"), location, parent)
    , save_(std::move(save))
    , name_(new QLineEdit(current.name, this))
    , email_(new QLineEdit(current.email, this))
    , repositoryOnly_(new QCheckBox(tr("For this repository only"), this))
{
    auto *explanation = new QLabel(this);
    Paragraph::setText(explanation, tr("Git signs each commit with a name and an email, and does not know yours "
                                       "yet. They go in git's configuration, as git config user.name and "
                                       "user.email would put them."));
    name_->setPlaceholderText(tr("Name"));
    email_->setPlaceholderText(tr("Email"));
    for (QLineEdit *field : {name_, email_}) {
        connect(field, &QLineEdit::returnPressed, this, &IdentityPopup::accept);
        connect(field, &QLineEdit::textEdited, this, [this] { showError({}); });
    }

    auto *cancel = new QPushButton(tr("Cancel"), this);
    auto *saveButton = new QPushButton(tr("Save and Commit"), this);
    saveButton->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    connect(saveButton, &QPushButton::clicked, this, &IdentityPopup::accept);
    setIconName(QStringLiteral("branch"));
    setFooterHint(tr("Enter to save · Esc to cancel"));
    addFooterButton(cancel);
    addFooterButton(saveButton);

    content()->addWidget(explanation);
    content()->addWidget(name_);
    content()->addWidget(email_);
    content()->addWidget(repositoryOnly_);
}

void IdentityPopup::accept()
{
    const GitOperations::Identity identity{.name = name_->text().trimmed(), .email = email_->text().trimmed()};
    if (identity.name.isEmpty() || identity.email.isEmpty()) {
        showError(tr("Give both a name and an email"));
        return;
    }
    const auto scope =
        repositoryOnly_->isChecked() ? GitOperations::ConfigScope::Repository : GitOperations::ConfigScope::Global;
    // Copied first: once dismissed, the popup goes.
    const auto save = save_;
    dismiss();
    save(identity, scope);
}

void IdentityPopup::focusContent()
{
    if (name_->text().isEmpty())
        name_->setFocus();
    else
        email_->setFocus();
}
