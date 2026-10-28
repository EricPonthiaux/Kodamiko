// SPDX-License-Identifier: GPL-3.0-only

#include "NewAgentPopup.h"

#include "AgentProvider.h"
#include "git/Branches.h"
#include "ui/Paragraph.h"
#include "ui/PopupFooter.h"
#include "ui/PopupMetrics.h"
#include "ui/PopupWidgets.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {

// The popup's own rules, besides the shared ones (popupStyleSheet).
constexpr const char *agentRules = R"(
QLabel#agentMessage[error="false"] { color: @muted; }
)";

} // namespace

NewAgentPopup::NewAgentPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("newAgent"), QSizeF(0.36, 0.3), QSize(540, 300), parent)
    , header_(new PopupHeader(QString(), QString(), this))
    , name_(new QLineEdit(this))
    , message_(new QLabel(this))
    , changesNote_(new QLabel(this))
    , start_(new QPushButton(tr("Start Session"), this))
{
    header_->setSubtitle(tr("A session besides the main one, in a git worktree of its own"));
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);

    name_->setObjectName(QStringLiteral("agentName"));
    connect(name_, &QLineEdit::textEdited, this, &NewAgentPopup::onNameEdited);
    connect(name_, &QLineEdit::textChanged, this, &NewAgentPopup::validate);
    connect(name_, &QLineEdit::returnPressed, this, &NewAgentPopup::start);
    message_->setObjectName(QStringLiteral("agentMessage"));
    changesNote_->setObjectName(QStringLiteral("settingsDescription"));
    Paragraph::setText(changesNote_,
                       tr("The worktree starts from the last commit: your uncommitted changes will not be in it."));
    auto *nameBlock = new QWidget(this);
    auto *nameLayout = new QVBoxLayout(nameBlock);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(6);
    nameLayout->addWidget(new QLabel(tr("Name of the session, of its worktree and of its branch"), this));
    nameLayout->addWidget(name_);
    nameLayout->addWidget(message_);
    nameLayout->addWidget(changesNote_);

    auto *card = new SettingsCard(this);
    card->addWidget(nameBlock);
    auto *body = new QVBoxLayout;
    body->setContentsMargins(PopupMetrics::bodyMargins);
    body->addWidget(card);
    body->addStretch();

    auto *footer = new PopupFooter(this);
    footer->setHint(tr("Enter to start · Esc to cancel"));
    auto *cancel = new QPushButton(tr("Cancel"), footer);
    start_->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    connect(start_, &QPushButton::clicked, this, &NewAgentPopup::start);
    footer->addButton(cancel);
    footer->addButton(start_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addLayout(body, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

void NewAgentPopup::setProvider(const AgentProvider &provider)
{
    header_->setTitle(tr("New %1 Session in Worktree").arg(provider.name()));
    header_->setIconName(provider.iconName());
}

void NewAgentPopup::applyTheme(const Theme &theme)
{
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(agentRules)));
    header_->applyTheme(theme);
}

void NewAgentPopup::popup(const QString &suggestedName, UncommittedChanges changes)
{
    load(suggestedName, changes);
    showCentered();
    name_->setFocus();
    name_->selectAll();
}

void NewAgentPopup::load(const QString &suggestedName, UncommittedChanges changes)
{
    name_->setText(suggestedName);
    changesNote_->setVisible(changes == UncommittedChanges::Some);
    validate();
}

void NewAgentPopup::onNameEdited(const QString &text)
{
    // Spaces become dashes as they are typed; the cursor stays in place.
    const QString name = Branches::sanitizedName(text);
    if (name == text)
        return;
    const int cursor = int(Branches::sanitizedName(text.left(name_->cursorPosition())).size());
    name_->setText(name);
    name_->setCursorPosition(cursor);
}

void NewAgentPopup::validate()
{
    const QString name = name_->text();
    const QString error = name.isEmpty() ? QString() : Branches::nameError(name);
    const bool hasError = !error.isEmpty();
    Paragraph::setText(message_,
                       hasError ? error : tr("Its branch: kodamiko/%1").arg(name.isEmpty() ? tr("<name>") : name));
    message_->setProperty("error", hasError);
    // The property only restyles after a re-polish.
    message_->style()->unpolish(message_);
    message_->style()->polish(message_);
    start_->setEnabled(!name.isEmpty() && !hasError);
}

void NewAgentPopup::start()
{
    if (!start_->isEnabled())
        return;
    emit startRequested(name_->text());
    dismiss();
}

void NewAgentPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        start();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
