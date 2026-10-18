#include "NewBranchPopup.h"

#include "git/Branches.h"
#include "ui/AppFonts.h"
#include "ui/PopupWidgets.h"

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
QLineEdit#branchName { padding: 7px 10px; border-radius: 8px; }
QLabel#branchMessage[error="true"] { color: #e06c75; }
QLabel#branchMessage[error="false"] { color: @muted; }
QLabel#branchOrigin { color: @muted; }
QPushButton:default:disabled { color: @muted; background: @panel; border: 1px solid @line; }
)";

const QString switchSetting = QStringLiteral("newBranch/switchTo");

} // namespace

NewBranchPopup::NewBranchPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("newBranch"), QSizeF(0.36, 0.3), QSize(540, 330), parent)
    , theme_(theme)
    , headerIcon_(new QLabel(this))
    , origin_(new QLabel(this))
    , name_(new QLineEdit(this))
    , message_(new QLabel(this))
    , switchTo_(new ToggleSwitch(this))
    , create_(new QPushButton(tr("Create Branch"), this))
{
    // The header: where the branch starts.
    auto *title = new QLabel(tr("New Branch"), this);
    AppFonts::setInterfaceFont(title, 1.5);
    origin_->setObjectName(QStringLiteral("branchOrigin"));
    auto *heading = new QVBoxLayout;
    heading->setSpacing(4);
    heading->addWidget(title);
    heading->addWidget(origin_);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(28, 22, 28, 0);
    header->setSpacing(14);
    header->addWidget(headerIcon_, 0, Qt::AlignTop);
    header->addLayout(heading, 1);

    // The card: the name, then the switch.
    name_->setObjectName(QStringLiteral("branchName"));
    name_->setPlaceholderText(tr("feature/my-change"));
    connect(name_, &QLineEdit::textEdited, this, &NewBranchPopup::onNameEdited);
    connect(name_, &QLineEdit::textChanged, this, &NewBranchPopup::validate);
    connect(name_, &QLineEdit::returnPressed, this, &NewBranchPopup::create);
    message_->setObjectName(QStringLiteral("branchMessage"));
    message_->setWordWrap(true);
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
    body->setContentsMargins(28, 0, 28, 20);
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
    footerLayout->setContentsMargins(28, 12, 18, 14);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(hint, 1);
    footerLayout->addWidget(cancel);
    footerLayout->addWidget(create_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addSpacing(18);
    layout->addLayout(body, 1);
    layout->addWidget(footer);

    applyTheme(theme);
}

void NewBranchPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    setStyleSheet(popupStyleSheet(theme, QString::fromLatin1(branchRules)));
    headerIcon_->setPixmap(iconTile(QStringLiteral("branch"), theme, 40, devicePixelRatioF()));
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
    if (repository.unborn)
        origin_->setText(tr("In a repository without commits yet: the first commit will go to it."));
    else if (repository.detached)
        origin_->setText(tr("From the detached HEAD, commit %1").arg(repository.headId));
    else
        origin_->setText(tr("From %1, commit %2 — %3")
                             .arg(repository.branch, repository.headId,
                                  repository.headMessage.section(QLatin1Char('\n'), 0, 0).trimmed()));
    origin_->setToolTip(origin_->text());
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
    message_->setText(hasError ? error : tr("Spaces become dashes; use \"/\" to group branches (feature/…)."));
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
