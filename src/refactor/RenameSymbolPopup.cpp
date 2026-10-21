// SPDX-License-Identifier: GPL-3.0-only

#include "RenameSymbolPopup.h"

#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

RenameSymbolPopup::RenameSymbolPopup(const QString &name, const QString &location, Rename rename, QWidget *parent)
    : EntryPopup(QStringLiteral("renameSymbolPopup"), tr("Rename “%1”").arg(name), location, parent)
    , rename_(std::move(rename))
    , field_(new QLineEdit(name, this))
{
    field_->selectAll();
    connect(field_, &QLineEdit::returnPressed, this, &RenameSymbolPopup::accept);
    connect(field_, &QLineEdit::textEdited, this, [this] { showError({}); });
    content()->addWidget(field_);
    setFooterHint(tr("Enter to rename · Alt+P to preview"));

    auto *cancel = new QPushButton(tr("Cancel"), this);
    auto *preview = new QPushButton(tr("&Preview"), this);
    auto *refactor = new QPushButton(tr("Refactor"), this);
    refactor->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    connect(preview, &QPushButton::clicked, this, &RenameSymbolPopup::preview);
    connect(refactor, &QPushButton::clicked, this, &RenameSymbolPopup::accept);
    addFooterButton(cancel);
    addFooterButton(preview);
    addFooterButton(refactor);
}

void RenameSymbolPopup::preview()
{
    submit(Mode::Preview);
}

void RenameSymbolPopup::accept()
{
    submit(Mode::Refactor);
}

void RenameSymbolPopup::focusContent()
{
    field_->setFocus();
}

void RenameSymbolPopup::submit(Mode mode)
{
    const QString message = rename_(field_->text().trimmed(), mode);
    if (message.isEmpty())
        dismiss();
    else
        showError(message);
}
