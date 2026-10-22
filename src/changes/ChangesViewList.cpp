// SPDX-License-Identifier: GPL-3.0-only

// ChangesView: the two file lists, "Unstaged" and "Staged", each under its
// own bar; their files in their folders, selected and acted on.

#include "ChangeTree.h"
#include "ChangesView.h"
#include "DiffView.h"
#include "FileChangeDelegate.h"
#include "git/GitCommands.h"
#include "ui/ContextMenus.h"
#include "ui/ViewHeader.h"

#include <QMenu>
#include <QTreeWidget>

#include <algorithm>

namespace {

constexpr int sectionRole = Qt::UserRole;
constexpr int indexRole = Qt::UserRole + 1; // -1 on a section's row or a folder
constexpr int treeIndentation = 14;

} // namespace

void ChangesView::setUpList(Section section)
{
    QTreeWidget *tree = list(section);
    tree->setObjectName(section == Section::Staged ? QStringLiteral("stagedList") : QStringLiteral("unstagedList"));
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(false);
    tree->setIndentation(treeIndentation);
    tree->setItemDelegate(new FileChangeDelegate(tree));
    tree->setFrameShape(QFrame::NoFrame);
    tree->setMouseTracking(true); // hover
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    tree->setToolTip(tr("Double-click: open the file in the editor\nRight-click: stage, unstage, discard"));
    // The section's own row, out of view: its files show at the top level,
    // and it stands for them all.
    auto *root = new QTreeWidgetItem(tree);
    root->setData(0, sectionRole, static_cast<int>(section));
    root->setData(0, indexRole, -1);
    root->setExpanded(true);
    tree->setRootIndex(tree->model()->index(0, 0));

    connect(tree, &QTreeWidget::currentItemChanged, this, [this, tree] { onCurrentItemChanged(tree); });
    connect(tree, &QTreeWidget::itemClicked, this, [this, tree] { onCurrentItemChanged(tree); });
    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this, section] { onSelectionChanged(section); });
    connect(tree, &QTreeWidget::customContextMenuRequested, this,
            [this, tree](const QPoint &position) { showListMenu(tree, position); });
    // A folder closed stays closed when the list is rebuilt.
    connect(tree, &QTreeWidget::itemCollapsed, this,
            [this](const QTreeWidgetItem *item) { collapsedFolders_.insert(keyOf(item)); });
    connect(tree, &QTreeWidget::itemExpanded, this,
            [this](const QTreeWidgetItem *item) { collapsedFolders_.remove(keyOf(item)); });
    connect(tree, &QTreeWidget::itemActivated, this, &ChangesView::openItem);
}

QTreeWidget *ChangesView::list(Section section) const
{
    switch (section) {
    case Section::Unstaged:
        return unstagedList_;
    case Section::Staged:
        return stagedList_;
    }
    return nullptr;
}

ViewHeader *ChangesView::listHeader(Section section) const
{
    switch (section) {
    case Section::Unstaged:
        return unstagedHeader_;
    case Section::Staged:
        return stagedHeader_;
    }
    return nullptr;
}

QTreeWidgetItem *ChangesView::sectionItem(Section section) const
{
    return list(section)->topLevelItem(0);
}

ChangesView::ItemKey ChangesView::keyOf(const QTreeWidgetItem *item) const
{
    const int index = item->data(0, indexRole).toInt();
    const auto section = static_cast<Section>(item->data(0, sectionRole).toInt());
    const bool isFile = index >= 0 && status_;
    if (isFile)
        return {static_cast<int>(section), files(section).at(index).change.path};
    // A folder by its path and a "/", apart from a file of that name.
    const QString folder = item->data(0, FileChangeDelegate::folderRole).toString();
    return {static_cast<int>(section), folder.isEmpty() ? QString() : folder + QLatin1Char('/')};
}

QList<ChangesView::ItemKey> ChangesView::selectedKeys() const
{
    QList<ItemKey> keys;
    for (const Section section : {Section::Unstaged, Section::Staged}) {
        for (const QTreeWidgetItem *item : list(section)->selectedItems())
            keys.append(keyOf(item));
    }
    return keys;
}

void ChangesView::restoreSelection(const QList<ItemKey> &selected, const std::optional<ItemKey> &current)
{
    for (const Section section : {Section::Unstaged, Section::Staged}) {
        QTreeWidget *tree = list(section);
        const QSignalBlocker blocker(tree);
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            const ItemKey key = keyOf(*it);
            if (selected.contains(key))
                (*it)->setSelected(true);
            if (current && key == *current)
                tree->setCurrentItem(*it, 0, QItemSelectionModel::NoUpdate);
        }
    }
}

void ChangesView::selectNow(const QString &relativePath)
{
    if (!status_)
        return;
    const auto matches = [&relativePath](const QString &path) {
        return relativePath.isEmpty() || path == relativePath || path.startsWith(relativePath + QLatin1Char('/'));
    };
    for (const Section section : {Section::Unstaged, Section::Staged}) {
        const QList<FileDiff> &diffs = files(section);
        const auto found = std::ranges::find_if(diffs, [&](const FileDiff &diff) { return matches(diff.change.path); });
        if (found == diffs.end())
            continue;
        QTreeWidgetItem *item = fileRow(section, int(found - diffs.begin()));
        QTreeWidget *tree = list(section);
        tree->clearSelection();
        tree->setCurrentItem(item);
        tree->scrollToItem(item);
        return;
    }
}

void ChangesView::rebuildList()
{
    for (const Section section : {Section::Staged, Section::Unstaged}) {
        const QSignalBlocker blocker(list(section));
        list(section)->clearSelection();
        QTreeWidgetItem *root = sectionItem(section);
        qDeleteAll(root->takeChildren());
        const QList<FileDiff> &diffs = files(section);
        listHeader(section)->setDetail(status_ ? QString::number(diffs.size()) : QString());
        if (!status_)
            continue;
        QStringList paths;
        for (const FileDiff &diff : diffs)
            paths.append(diff.change.path);
        const QList<QTreeWidgetItem *> rows = ChangeTree::addFiles(root, paths);
        for (int i = 0; i < diffs.size(); ++i) {
            FileChangeDelegate::setChange(rows[i], diffs[i].change);
            FileChangeDelegate::setStats(rows[i], FilePatch::parse(diffs[i].patch));
            rows[i]->setData(0, indexRole, i);
        }
        markFolders(root, section);
    }
}

void ChangesView::markFolders(QTreeWidgetItem *parent, Section section)
{
    for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem *row = parent->child(i);
        row->setData(0, sectionRole, static_cast<int>(section));
        if (!FileChangeDelegate::isFolder(row))
            continue;
        row->setData(0, indexRole, -1);
        markFolders(row, section);
        if (collapsedFolders_.contains(keyOf(row)))
            row->setExpanded(false);
    }
}

QTreeWidgetItem *ChangesView::fileRow(Section section, int index) const
{
    for (QTreeWidgetItem *row : ChangeTree::filesUnder(sectionItem(section))) {
        if (row->data(0, indexRole).toInt() == index)
            return row;
    }
    return nullptr;
}

void ChangesView::onCurrentItemChanged(const QTreeWidget *tree)
{
    QTreeWidgetItem *item = tree->currentItem();
    if (!item || !status_)
        return;
    const auto section = static_cast<Section>(item->data(0, sectionRole).toInt());
    if (section != shownSection_)
        showSection(section);
    // A folder: its first file.
    const QList<QTreeWidgetItem *> rows = ChangeTree::filesUnder(item);
    const int index = rows.isEmpty() ? -1 : rows.first()->data(0, indexRole).toInt();
    diff_->setFirstVisibleLine(index >= 0 ? diff_->fileStart(index) : 0);
}

void ChangesView::onSelectionChanged(Section section)
{
    // One list's selection at a time: the diff shows one section.
    const Section other = section == Section::Staged ? Section::Unstaged : Section::Staged;
    if (!list(section)->selectedItems().isEmpty()) {
        const QSignalBlocker blocker(list(other));
        list(other)->clearSelection();
        list(other)->viewport()->update();
    }
    updateActions();
}

void ChangesView::openItem(const QTreeWidgetItem *item)
{
    const int index = item->data(0, indexRole).toInt();
    if (index < 0 || !status_)
        return;
    const FileChange &change = files(static_cast<Section>(item->data(0, sectionRole).toInt())).at(index).change;
    if (change.kind != ChangeKind::Deleted)
        emit openFileRequested(change.path, 0);
}

QList<ChangesView::FileRef> ChangesView::selectedFiles() const
{
    QList<FileRef> result;
    if (!status_)
        return result;
    const auto add = [&result](Section section, int index) {
        const bool known = std::ranges::any_of(
            result, [&](const FileRef &ref) { return ref.section == section && ref.index == index; });
        if (!known)
            result.append({section, index});
    };
    // A folder stands for the files under it.
    for (const Section section : {Section::Unstaged, Section::Staged}) {
        for (QTreeWidgetItem *item : list(section)->selectedItems()) {
            for (const QTreeWidgetItem *row : ChangeTree::filesUnder(item))
                add(section, row->data(0, indexRole).toInt());
        }
    }
    return result;
}

void ChangesView::showListMenu(const QTreeWidget *tree, const QPoint &position)
{
    const QList<FileRef> selection = selectedFiles();
    if (selection.isEmpty())
        return;
    const bool busy = commands_->isBusy();
    const auto count = [&selection](Section section) {
        return std::ranges::count_if(selection, [section](const FileRef &ref) { return ref.section == section; });
    };

    QMenu menu;
    if (count(Section::Unstaged) > 0) {
        menu.addAction(tr("Stage"), this, [this, selection] { stageFiles(selection); })->setEnabled(!busy);
        menu.addAction(tr("Discard Changes…"), this, [this, selection] { discardFiles(selection); })->setEnabled(!busy);
    }
    if (count(Section::Staged) > 0)
        menu.addAction(tr("Unstage"), this, [this, selection] { unstageFiles(selection); })->setEnabled(!busy);
    if (selection.size() == 1) {
        const FileChange &change = files(selection.first().section).at(selection.first().index).change;
        if (change.kind != ChangeKind::Deleted) {
            menu.addSeparator();
            menu.addAction(tr("Open File"), this, [this, path = change.path] { emit openFileRequested(path, 0); });
        }
    }
    ContextMenus::exec(menu, *tree, tree->viewport()->mapToGlobal(position));
}
