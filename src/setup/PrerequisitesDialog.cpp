#include "PrerequisitesDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int statusIconSize = 20;

QString statusText(const Prerequisite &prerequisite)
{
    switch (prerequisite.status) {
    case Prerequisite::Status::Missing:
        return PrerequisitesDialog::tr("Not found");
    case Prerequisite::Status::Outdated:
        return PrerequisitesDialog::tr("Version %1 — version %2 or newer is needed")
            .arg(prerequisite.version.toString(), prerequisite.minimumVersion.toString());
    case Prerequisite::Status::Ok:
        return prerequisite.version.isNull()
                   ? QDir::toNativeSeparators(prerequisite.path)
                   : PrerequisitesDialog::tr("Version %1 — %2")
                         .arg(prerequisite.version.toString(), QDir::toNativeSeparators(prerequisite.path));
    }
    return {};
}

QIcon statusIcon(const QStyle *style, Prerequisite::Status status)
{
    switch (status) {
    case Prerequisite::Status::Missing:
        return style->standardIcon(QStyle::SP_MessageBoxCritical);
    case Prerequisite::Status::Outdated:
        return style->standardIcon(QStyle::SP_MessageBoxWarning);
    case Prerequisite::Status::Ok:
        return style->standardIcon(QStyle::SP_DialogApplyButton);
    }
    return {};
}

} // namespace

PrerequisitesDialog::PrerequisitesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Prerequisites"));
    setMinimumWidth(620);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);

    rows_ = new QWidget(this);
    rowsLayout_ = new QVBoxLayout(rows_);
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    rowsLayout_->setSpacing(12);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    const QPushButton *checkAgain = buttons->addButton(tr("Check Again"), QDialogButtonBox::ActionRole);
    connect(checkAgain, &QPushButton::clicked, this, &PrerequisitesDialog::recheck);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(16);
    layout->addWidget(summary_);
    layout->addWidget(rows_);
    layout->addStretch();
    layout->addWidget(buttons);

    recheck();
}

bool PrerequisitesDialog::allOk() const
{
    return std::all_of(prerequisites_.begin(), prerequisites_.end(),
                       [](const Prerequisite &p) { return p.status == Prerequisite::Status::Ok; });
}

void PrerequisitesDialog::recheck()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    prerequisites_ = Prerequisites::check();
    QApplication::restoreOverrideCursor();
    rebuild();
    emit checked();
}

void PrerequisitesDialog::rebuild()
{
    while (const QLayoutItem *item = rowsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    summary_->setText(allOk() ? tr("Everything the editor needs is installed.")
                              : tr("Some programs the editor relies on are missing or too old. Install them, then "
                                   "click <b>Check Again</b>. Installations run in a terminal tab, where you can "
                                   "follow them and answer their questions."));

    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (const Prerequisite &prerequisite : std::as_const(prerequisites_)) {
        auto *card = new QFrame(rows_);
        card->setFrameShape(QFrame::StyledPanel);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(6);

        auto *icon = new QLabel(card);
        icon->setPixmap(statusIcon(style(), prerequisite.status).pixmap(statusIconSize, statusIconSize));
        auto *name = new QLabel(QStringLiteral("<b>%1</b>").arg(prerequisite.name.toHtmlEscaped()), card);
        auto *status = new QLabel(statusText(prerequisite), card);
        status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        status->setEnabled(prerequisite.status != Prerequisite::Status::Ok); // path: secondary text
        auto *title = new QHBoxLayout;
        title->setSpacing(8);
        title->addWidget(icon);
        title->addWidget(name);
        title->addWidget(status, 1);
        cardLayout->addLayout(title);

        auto *purpose = new QLabel(prerequisite.purpose, card);
        purpose->setWordWrap(true);
        purpose->setEnabled(false); // secondary text
        cardLayout->addWidget(purpose);

        if (prerequisite.status != Prerequisite::Status::Ok) {
            if (!prerequisite.installCommand.isEmpty()) {
                auto *command = new QLineEdit(prerequisite.installCommand, card);
                command->setReadOnly(true);
                command->setFont(mono);
                command->setCursorPosition(0);
                auto *copy = new QPushButton(tr("Copy"), card);
                connect(copy, &QPushButton::clicked, this,
                        [text = prerequisite.installCommand] { QApplication::clipboard()->setText(text); });
                auto *install = new QPushButton(
                    prerequisite.status == Prerequisite::Status::Outdated ? tr("Update") : tr("Install"), card);
                install->setDefault(true);
                connect(install, &QPushButton::clicked, this, [this, prerequisite] {
                    emit installRequested(tr("Install %1").arg(prerequisite.name),
                                          Prerequisites::installProcess(prerequisite, QDir::homePath()));
                });
                auto *commandRow = new QHBoxLayout;
                commandRow->addWidget(command, 1);
                commandRow->addWidget(copy);
                commandRow->addWidget(install);
                cardLayout->addLayout(commandRow);
            }
            auto *guide =
                new QLabel(tr("<a href=\"%1\">Installation guide</a>").arg(prerequisite.helpUrl.toString()), card);
            guide->setOpenExternalLinks(true);
            cardLayout->addWidget(guide);
        }
        rowsLayout_->addWidget(card);
    }
    adjustSize();
}
