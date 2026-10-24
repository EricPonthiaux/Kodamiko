// SPDX-License-Identifier: GPL-3.0-only

#include "AgentCheckPopup.h"

#include "ui/Paragraph.h"
#include "ui/PopupFooter.h"
#include "ui/PopupMetrics.h"
#include "ui/PopupWidgets.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

AgentCheckPopup::AgentCheckPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("agentCheck"), QSizeF(0.4, 0.28), QSize(600, 280), parent)
    , header_(new PopupHeader(QStringLiteral("check"), tr("Check Before Merging Agents"), this))
    , command_(new QLineEdit(this))
{
    header_->setSubtitle(tr("Saved with the project"));
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);

    command_->setObjectName(QStringLiteral("agentCheckCommand"));
    command_->setPlaceholderText(tr("No check"));
    connect(command_, &QLineEdit::returnPressed, this, &AgentCheckPopup::save);
    auto *description = new QLabel(this);
    description->setObjectName(QStringLiteral("settingsDescription"));
    Paragraph::setText(description,
                       tr("Run in an agent's worktree once its commits are replayed on the current branch; if it "
                          "fails, the agent is told to fix it. Empty: its commits are merged without a check.\n"
                          "Example: cmake -B build && cmake --build build && ctest --test-dir build"));
    description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *commandBlock = new QWidget(this);
    auto *commandLayout = new QVBoxLayout(commandBlock);
    commandLayout->setContentsMargins(0, 0, 0, 0);
    commandLayout->setSpacing(6);
    commandLayout->addWidget(new QLabel(tr("Shell command"), this));
    commandLayout->addWidget(command_);
    commandLayout->addWidget(description);

    auto *card = new SettingsCard(this);
    card->addWidget(commandBlock);
    auto *body = new QVBoxLayout;
    body->setContentsMargins(PopupMetrics::bodyMargins);
    body->addWidget(card);
    body->addStretch();

    auto *footer = new PopupFooter(this);
    footer->setHint(tr("Enter to save · Esc to cancel"));
    auto *cancel = new QPushButton(tr("Cancel"), footer);
    auto *saveButton = new QPushButton(tr("Save"), footer);
    saveButton->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    connect(saveButton, &QPushButton::clicked, this, &AgentCheckPopup::save);
    footer->addButton(cancel);
    footer->addButton(saveButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addLayout(body, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

void AgentCheckPopup::popup(const QString &command)
{
    command_->setText(command);
    showCentered();
    command_->setFocus();
}

void AgentCheckPopup::applyTheme(const Theme &theme)
{
    setStyleSheet(popupStyleSheet(theme));
    header_->applyTheme(theme);
}

void AgentCheckPopup::save()
{
    emit saved(command_->text().trimmed());
    dismiss();
}

void AgentCheckPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        save();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
