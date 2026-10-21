// SPDX-License-Identifier: GPL-3.0-only

#include "UsagesView.h"

#include "ui/FileIcons.h"
#include "ui/TreeColumnFit.h"
#include "ui/ViewHeader.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// Item data roles.
constexpr int pathRole = Qt::UserRole;
constexpr int lineRole = Qt::UserRole + 1;
constexpr int columnRole = Qt::UserRole + 2;

// Earlier searches kept in the history.
constexpr qsizetype maximumHistory = 10;

constexpr std::initializer_list<UsageKind> allKinds = {UsageKind::Declaration, UsageKind::Read,   UsageKind::Write,
                                                       UsageKind::Call,        UsageKind::Import, UsageKind::Textual};

QString kindName(UsageKind kind)
{
    switch (kind) {
    case UsageKind::Declaration:
        return UsagesView::tr("Declarations");
    case UsageKind::Read:
        return UsagesView::tr("Reads");
    case UsageKind::Write:
        return UsagesView::tr("Writes");
    case UsageKind::Call:
        return UsagesView::tr("Calls");
    case UsageKind::Import:
        return UsagesView::tr("Imports");
    case UsageKind::Textual:
        return UsagesView::tr("Comments and Strings");
    }
    return {};
}

// After the line of a usage that is not a mere read.
QString kindTag(UsageKind kind)
{
    switch (kind) {
    case UsageKind::Declaration:
        return UsagesView::tr("declaration");
    case UsageKind::Write:
        return UsagesView::tr("write");
    case UsageKind::Call:
        return UsagesView::tr("call");
    case UsageKind::Import:
        return UsagesView::tr("import");
    case UsageKind::Textual:
        return UsagesView::tr("text");
    case UsageKind::Read:
        break;
    }
    return {};
}

QString usageText(const Usage &usage)
{
    const QString tag = kindTag(usage.kind);
    const QString line = QStringLiteral("%1  %2").arg(usage.line + 1).arg(usage.lineText.trimmed());
    return tag.isEmpty() ? line : QStringLiteral("%1   · %2").arg(line, tag);
}

} // namespace

UsagesView::UsagesView(QWidget *parent)
    : QWidget(parent)
    , header_(new ViewHeader(tr("Usages"), this))
    , filterMenu_(new QMenu(this))
    , historyMenu_(new QMenu(this))
    , tree_(new QTreeWidget(this))
    , placeholder_(new QLabel(tr("Find Usages (Alt+F7) lists here\nthe usages of the name at the cursor"), this))
    , stack_(new QStackedWidget(this))
{
    connect(header_->addButton(QStringLiteral("refresh"), tr("Rerun the Search")), &QToolButton::clicked, this, [this] {
        if (current_ >= 0)
            emit rerunRequested(history_[current_].search);
    });
    // Menu buttons, without the arrow over their icon.
    const auto addMenuButton = [this](const QString &iconName, const QString &toolTip, QMenu *menu) {
        QToolButton *button = header_->addButton(iconName, toolTip);
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0; }"));
    };
    addMenuButton(QStringLiteral("filter"), tr("Kinds of Usages Shown"), filterMenu_);
    addMenuButton(QStringLiteral("history"), tr("Earlier Searches"), historyMenu_);
    connect(historyMenu_, &QMenu::aboutToShow, this, &UsagesView::showHistoryMenu);
    createFilters();

    tree_->header()->hide();
    tree_->setUniformRowHeights(true);
    tree_->setAnimated(false);
    tree_->setExpandsOnDoubleClick(false);
    columnFit_ = new TreeColumnFit(tree_);
    connect(tree_, &QTreeWidget::itemClicked, this, &UsagesView::activate);
    connect(tree_, &QTreeWidget::itemActivated, this, &UsagesView::activate);

    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setEnabled(false);
    stack_->addWidget(placeholder_);
    stack_->addWidget(tree_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(stack_, 1);
}

void UsagesView::createFilters()
{
    for (const UsageKind kind : allKinds) {
        QAction *action = filterMenu_->addAction(kindName(kind));
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::toggled, this, [this, kind](bool shown) {
            if (shown)
                hiddenKinds_.remove(kind);
            else
                hiddenKinds_.insert(kind);
            rebuild();
        });
        filters_.append({kind, action});
    }
}

QAction *UsagesView::filterAction(UsageKind kind) const
{
    for (const auto &[filterKind, action] : filters_) {
        if (filterKind == kind)
            return action;
    }
    return nullptr;
}

void UsagesView::setProjectFolder(const QString &folder)
{
    folder_ = folder;
    history_.clear();
    current_ = -1;
    rebuild();
}

void UsagesView::setDarkIcons(bool dark)
{
    if (dark == darkIcons_)
        return;
    darkIcons_ = dark;
    rebuild();
}

void UsagesView::setIconColor(const QColor &color)
{
    header_->setIconColor(color);
}

void UsagesView::showResults(const UsageResults &results)
{
    history_.prepend(results);
    if (history_.size() > maximumHistory)
        history_.removeLast();
    current_ = 0;
    rebuild();
}

void UsagesView::showHistoryMenu()
{
    historyMenu_->clear();
    for (int i = 0; i < history_.size(); ++i) {
        QAction *action = historyMenu_->addAction(
            QStringLiteral("%1  (%2)").arg(history_[i].search.name).arg(history_[i].usages.size()));
        action->setCheckable(true);
        action->setChecked(i == current_);
        connect(action, &QAction::triggered, this, [this, i] {
            current_ = i;
            rebuild();
        });
    }
    if (history_.isEmpty())
        historyMenu_->addAction(tr("No searches yet"))->setEnabled(false);
}

QString UsagesView::describe(const UsageResults &results, int shown) const
{
    const QString name = results.search.name;
    const int found = int(results.usages.size());
    QString text = results.isSemantic ? tr("%n usage(s) of “%1”", nullptr, found).arg(name)
                                      : tr("%n occurrence(s) of the word “%1”", nullptr, found).arg(name);
    if (shown != found)
        text += tr(", %1 shown").arg(shown);
    return text;
}

void UsagesView::rebuild()
{
    tree_->setUpdatesEnabled(false);
    tree_->clear();
    if (current_ < 0) {
        header_->setDetail({});
        stack_->setCurrentWidget(placeholder_);
        tree_->setUpdatesEnabled(true);
        return;
    }
    const UsageResults &results = history_[current_];
    QMap<QString, QList<Usage>> byFile;
    int shown = 0;
    for (const Usage &usage : results.usages) {
        if (hiddenKinds_.contains(usage.kind))
            continue;
        byFile[usage.path].append(usage);
        ++shown;
    }
    for (auto it = byFile.cbegin(); it != byFile.cend(); ++it)
        addFile(it.key(), it.value());
    tree_->expandAll();
    tree_->setUpdatesEnabled(true);
    columnFit_->refit();
    header_->setDetail(describe(results, shown));
    if (results.usages.isEmpty())
        placeholder_->setText(tr("No usages of “%1”").arg(results.search.name));
    stack_->setCurrentWidget(shown == 0 ? static_cast<QWidget *>(placeholder_) : tree_);
}

void UsagesView::addFile(const QString &path, const QList<Usage> &usages)
{
    const QFileInfo info(path);
    QString folder = folder_.isEmpty() ? info.absolutePath() : QDir(folder_).relativeFilePath(info.absolutePath());
    if (folder == QLatin1String("."))
        folder.clear();
    auto *fileItem = new QTreeWidgetItem(tree_);
    fileItem->setText(0, QStringLiteral("%1  %2").arg(info.fileName()).arg(usages.size()));
    fileItem->setIcon(0, FileIcons::file(info.fileName(), darkIcons_));
    fileItem->setToolTip(
        0, QDir::toNativeSeparators(folder.isEmpty() ? info.fileName() : folder + QLatin1Char('/') + info.fileName()));
    fileItem->setData(0, pathRole, path);
    for (const Usage &usage : usages) {
        auto *item = new QTreeWidgetItem(fileItem);
        item->setText(0, usageText(usage));
        item->setToolTip(0, usage.lineText.trimmed());
        item->setData(0, pathRole, path);
        item->setData(0, lineRole, usage.line);
        item->setData(0, columnRole, usage.column);
    }
}

void UsagesView::activate(QTreeWidgetItem *item)
{
    if (!item)
        return;
    if (!item->parent()) {
        // A file: its first usage.
        if (item->childCount() == 0)
            return;
        item = item->child(0);
    }
    emit usageActivated(item->data(0, pathRole).toString(), item->data(0, lineRole).toInt(),
                        item->data(0, columnRole).toInt());
}

QList<QTreeWidgetItem *> UsagesView::usageItems() const
{
    QList<QTreeWidgetItem *> items;
    for (int file = 0; file < tree_->topLevelItemCount(); ++file) {
        const QTreeWidgetItem *fileItem = tree_->topLevelItem(file);
        for (int usage = 0; usage < fileItem->childCount(); ++usage)
            items.append(fileItem->child(usage));
    }
    return items;
}

void UsagesView::goBy(int step)
{
    const QList<QTreeWidgetItem *> items = usageItems();
    if (items.isEmpty())
        return;
    const qsizetype selected = items.indexOf(tree_->currentItem());
    qsizetype next = 0;
    if (selected >= 0)
        next = (selected + step + items.size()) % items.size();
    else if (step < 0)
        next = items.size() - 1;
    tree_->setCurrentItem(items[next]);
    activate(items[next]);
}

void UsagesView::goToNext()
{
    goBy(1);
}

void UsagesView::goToPrevious()
{
    goBy(-1);
}
