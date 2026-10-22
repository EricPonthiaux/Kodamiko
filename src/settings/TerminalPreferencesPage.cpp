// SPDX-License-Identifier: GPL-3.0-only

#include "TerminalPreferencesPage.h"

#include "ui/PopupWidgets.h"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <memory>

namespace {

constexpr int listHeightPx = 160;

QListWidgetItem *folderItem(const QString &folder)
{
    auto *item = new QListWidgetItem(QDir::toNativeSeparators(folder));
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    return item;
}

} // namespace

TerminalPreferencesPage::TerminalPreferencesPage(QWidget *parent)
    : QWidget(parent)
    , folders_(new QListWidget(this))
{
    folders_->setObjectName(QStringLiteral("terminalPathFolders"));
    folders_->setMinimumHeight(listHeightPx);
    folders_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    connect(folders_, &QListWidget::currentRowChanged, this, &TerminalPreferencesPage::updateButtons);

    auto *description =
        new QLabel(tr("Put first in the PATH of the terminals, the run configurations and the agent's sessions, in "
                      "this order, before the folders of the project's recipe. Applies to the programs started "
                      "next."),
                   this);
    description->setObjectName(QStringLiteral("pageDescription"));
    description->setWordWrap(true);

    auto *card = new SettingsCard;
    card->addWidget(folders_);
    card->addWidget(createButtons());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(sectionHeading(tr("Extra PATH folders")));
    layout->addWidget(card);
    layout->addSpacing(8);
    layout->addWidget(description);
    updateButtons();
}

QWidget *TerminalPreferencesPage::createButtons()
{
    auto *buttons = new QWidget;
    auto *add = new QPushButton(tr("Add…"), buttons);
    remove_ = new QPushButton(tr("Remove"), buttons);
    up_ = new QPushButton(tr("Move Up"), buttons);
    down_ = new QPushButton(tr("Move Down"), buttons);
    connect(add, &QPushButton::clicked, this, &TerminalPreferencesPage::addFolder);
    connect(remove_, &QPushButton::clicked, this, &TerminalPreferencesPage::removeFolder);
    connect(up_, &QPushButton::clicked, this, [this] { moveFolder(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { moveFolder(1); });

    auto *layout = new QHBoxLayout(buttons);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(8);
    for (QPushButton *button : {add, remove_, up_, down_})
        layout->addWidget(button);
    layout->addStretch();
    return buttons;
}

void TerminalPreferencesPage::setPathFolders(const QStringList &folders)
{
    folders_->clear();
    for (const QString &folder : folders)
        folders_->addItem(folderItem(folder));
    updateButtons();
}

QStringList TerminalPreferencesPage::pathFolders() const
{
    QStringList folders;
    for (int row = 0; row < folders_->count(); ++row) {
        const QString folder = folders_->item(row)->text().trimmed();
        if (!folder.isEmpty())
            folders.append(QDir::fromNativeSeparators(folder));
    }
    return folders;
}

void TerminalPreferencesPage::addFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Folder to Put in the PATH"));
    if (folder.isEmpty())
        return;
    folders_->addItem(folderItem(folder));
    folders_->setCurrentRow(folders_->count() - 1);
}

void TerminalPreferencesPage::removeFolder()
{
    // takeItem() hands the item over.
    const std::unique_ptr<QListWidgetItem> removed(folders_->takeItem(folders_->currentRow()));
    updateButtons();
}

void TerminalPreferencesPage::moveFolder(int rows)
{
    const int from = folders_->currentRow();
    const int to = from + rows;
    if (from < 0 || to < 0 || to >= folders_->count())
        return;
    folders_->insertItem(to, folders_->takeItem(from));
    folders_->setCurrentRow(to);
}

void TerminalPreferencesPage::updateButtons()
{
    const int row = folders_->currentRow();
    remove_->setEnabled(row >= 0);
    up_->setEnabled(row > 0);
    down_->setEnabled(row >= 0 && row < folders_->count() - 1);
}
