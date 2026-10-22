// SPDX-License-Identifier: GPL-3.0-only

#include "NewBranchPopup.h"

#include "git/Branches.h"
#include "ui/Paragraph.h"
#include "ui/PopupWidgets.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {

// The popup's own rules, besides the shared ones (popupStyleSheet).
constexpr const char *branchRules = R"(
QLabel#branchMessage[error="true"] { color: #e06c75; }
QLabel#branchMessage[error="false"] { color: @muted; }
QPushButton:default:disabled { color: @muted; background: @panel; border: 1px solid @line; }
)";

const QString switchSetting = QStringLiteral("newBranch/switchTo");

} // namespace

NewBranchPopup::NewBranchPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("newBranch"), QSizeF(0.36, 0.3), QSize(540, 330), parent)
    , theme_(theme)
    , header_(new PopupHeader(QStringLiteral("branch"), tr("New Branch"), this))
    , name_(new QLineEdit(this))
    , message_(new QLabel(this))
    , switchTo_(new QCheckBox(this))
    , create_(new QPushButton(tr("Create Branch"), this))
{
    // The header says where the branch starts (see load()).
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);

    // The name, then whether to switch to it.
    name_->setObjectName(QStringLiteral("branchName"));
    name_->setPlaceholderText(tr("feature/my-change"));
    connect(name_, &QLineEdit::textEdited, this, &NewBranchPopup::onNameEdited);
    connect(name_, &QLineEdit::textChanged, this, &NewBranchPopup::validate);
    connect(name_, &QLineEdit::returnPressed, this, &NewBranchPopup::create);
    message_->setObjectName(QStringLiteral("branchMessage"));
    auto *nameLabel = new QLabel(tr("Name"), this);
    auto *nameBlock = new QWidget(this);
    auto *nameLayout = new QVBoxLayout(nameBlock);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(6);
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(name_);
    nameLayout->addWidget(message_);

    auto *card = new SettingsCard(this);
    card->addWidget(nameBlock);
    card->addRow(tr("Switch to it"), tr("Your uncommitted changes come along to the new branch."), switchTo_);
    switchTo_->setChecked(QSettings().value(switchSetting, true).toBool());
    connect(switchTo_, &QAbstractButton::toggled, this,
            [](bool checked) { QSettings().setValue(switchSetting, checked); });

    auto *body = new QVBoxLayout;
    body->setContentsMargins(16, 12, 16, 12);
    body->addWidget(card);
    body->addStretch();

    // The footer.
    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    auto *hint = new QLabel(tr("Enter to create · Esc to cancel"), footer);
    hint->setObjectName(QStringLiteral("footerHint"));
    auto *cancel = new QPushButton(tr("Cancel"), footer);
    cancel->setMinimumWidth(84);
    create_->setParent(footer);
    create_->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    connect(create_, &QPushButton::clicked, this, &NewBranchPopup::create);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 10, 16, 10);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(hint, 1);
    footerLayout->addWidget(cancel);
    footerLayout->addWidget(create_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addLayout(body, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

void NewBranchPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(branchRules)));
    header_->applyTheme(theme);
}

void NewBranchPopup::popup(const RepositoryInfo &repository, const QStringList &existing)
{
    load(repository, existing);
    showCentered();
    name_->setFocus();
}

void NewBranchPopup::load(const RepositoryInfo &repository, const QStringList &existing)
{
    existing_ = existing;
    QString origin;
    if (repository.unborn)
        origin = tr("In a repository without commits yet: the first commit will go to it.");
    else if (repository.detached)
        origin = tr("From the detached HEAD, commit %1").arg(repository.headId);
    else
        origin = tr("From %1, commit %2 — %3")
                     .arg(repository.branch, repository.headId,
                          repository.headMessage.section(QLatin1Char('\n'), 0, 0).trimmed());
    header_->setSubtitle(origin);
    header_->setToolTip(origin);
    name_->clear();
    validate();
}

void NewBranchPopup::onNameEdited(const QString &text)
{
    // Spaces become dashes as they are typed; the cursor stays in place.
    const QString name = Branches::sanitizedName(text);
    if (name == text)
        return;
    const int cursor = int(Branches::sanitizedName(text.left(name_->cursorPosition())).size());
    name_->setText(name);
    name_->setCursorPosition(cursor);
}

void NewBranchPopup::validate()
{
    const QString name = name_->text();
    QString error = name.isEmpty() ? QString() : Branches::nameError(name);
    if (error.isEmpty() && existing_.contains(name))
        error = tr("A branch named \"%1\" already exists.").arg(name);
    const bool hasError = !error.isEmpty();
    Paragraph::setText(message_,
                       hasError ? error : tr("Spaces become dashes; use \"/\" to group branches (feature/…)."));
    message_->setProperty("error", hasError);
    // The property only restyles after a re-polish.
    message_->style()->unpolish(message_);
    message_->style()->polish(message_);
    create_->setEnabled(!name.isEmpty() && !hasError);
}

void NewBranchPopup::create()
{
    if (!create_->isEnabled())
        return;
    emit createRequested(name_->text(), switchTo_->isChecked());
    dismiss();
}

void NewBranchPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        create();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
