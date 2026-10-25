// SPDX-License-Identifier: GPL-3.0-only

// ChangesView: the file list, a row per file with a box that stages or
// unstages it; its files in their folders, selected and acted on.

#include "ChangeTree.h"
#include "ChangesView.h"
#include "DiffView.h"
#include "FileChangeDelegate.h"
#include "git/GitCommands.h"
#include "ui/ContextMenus.h"

#include <QMenu>
#include <QShortcut>
#include <QTreeWidget>

#include <algorithm>

namespace {

// On a file's row, its index in RepositoryStatus::staged and ::unstaged; -1
// when it has no changes of that kind.
constexpr int stagedIndexRole = Qt::UserRole;
constexpr int unstagedIndexRole = Qt::UserRole + 1;
constexpr int treeIndentation = 14;

// A file of the list: its staged changes, its unstaged ones, or both.
struct ListedFile {
    QString path;
    int staged = -1;
    int unstaged = -1;
};

Qt::CheckState checkStateOf(const ListedFile &file)
{
    if (file.unstaged < 0)
        return Qt::Checked;
    return file.staged < 0 ? Qt::Unchecked : Qt::PartiallyChecked;
}

// The change a file's row shows. Changed both in the index and on disk:
// where the file comes from (added, renamed, conflicted), unless the disk
// deleted it since.
const FileChange &shownChange(const RepositoryStatus &status, const ListedFile &file)
{
    if (file.unstaged < 0)
        return status.staged[file.staged].change;
    const FileChange &unstaged = status.unstaged[file.unstaged].change;
    if (file.staged < 0)
        return unstaged;
    const FileChange &staged = status.staged[file.staged].change;
    const bool tellsTheOrigin = staged.kind != ChangeKind::Modified && staged.kind != ChangeKind::Deleted;
    return tellsTheOrigin && unstaged.kind != ChangeKind::Deleted ? staged : unstaged;
}

// The lines added and removed by both the file's staged and unstaged changes.
void setStats(QTreeWidgetItem *row, const RepositoryStatus &status, const ListedFile &file)
{
    int added = 0;
    int removed = 0;
    for (const auto &[diffs, index] :
         {std::pair{&status.staged, file.staged}, std::pair{&status.unstaged, file.unstaged}}) {
        if (index < 0)
            continue;
        const FilePatch patch = FilePatch::parse(diffs->at(index).patch);
        added += patch.addedLines();
        removed += patch.removedLines();
    }
    FileChangeDelegate::setStats(row, added, removed);
}

// The index a file's row holds in `role`; -1 on a folder, or for no changes.
int indexIn(const QTreeWidgetItem *row, int role)
{
    const QVariant index = row->data(0, role);
    return index.isValid() ? index.toInt() : -1;
}

} // namespace

void ChangesView::setUpList()
{
    QTreeWidget *tree = fileList_;
    tree->setObjectName(QStringLiteral("fileList"));
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(false);
    tree->setIndentation(treeIndentation);
    tree->setItemDelegate(new FileChangeDelegate(tree));
    tree->setFrameShape(QFrame::NoFrame);
    tree->setMouseTracking(true); // hover
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    tree->setToolTip(tr("Box or Space: stage or unstage the file\nDouble-click: open the file in the editor\n"
                        "Right-click: stage, unstage, discard"));
    // The list's own row, out of view: its files show at the top level, and
    // it stands for them all.
    auto *root = new QTreeWidgetItem(tree);
    root->setExpanded(true);
    tree->setRootIndex(tree->model()->index(0, 0));

    connect(tree, &QTreeWidget::currentItemChanged, this, &ChangesView::onCurrentItemChanged);
    connect(tree, &QTreeWidget::itemClicked, this, &ChangesView::onCurrentItemChanged);
    connect(tree, &QTreeWidget::itemSelectionChanged, this, &ChangesView::onSelectionChanged);
    connect(tree, &QTreeWidget::itemChanged, this, &ChangesView::onItemChanged);
    connect(tree, &QTreeWidget::customContextMenuRequested, this, &ChangesView::showListMenu);
    // A folder closed stays closed when the list is rebuilt.
    connect(tree, &QTreeWidget::itemCollapsed, this,
            [this](const QTreeWidgetItem *item) { collapsedFolders_.insert(keyOf(item)); });
    connect(tree, &QTreeWidget::itemExpanded, this,
            [this](const QTreeWidgetItem *item) { collapsedFolders_.remove(keyOf(item)); });
    connect(tree, &QTreeWidget::itemActivated, this, &ChangesView::openItem);
    const auto *space = new QShortcut(QKeySequence(Qt::Key_Space), tree, nullptr, nullptr, Qt::WidgetShortcut);
    connect(space, &QShortcut::activated, this, &ChangesView::toggleSelectedFiles);
}

QTreeWidgetItem *ChangesView::rootItem() const
{
    return fileList_->topLevelItem(0);
}

QList<ChangesView::FileRef> ChangesView::refsOf(const QTreeWidgetItem *row)
{
    QList<FileRef> refs;
    if (const int staged = indexIn(row, stagedIndexRole); staged >= 0)
        refs.append({Section::Staged, staged});
    if (const int unstaged = indexIn(row, unstagedIndexRole); unstaged >= 0)
        refs.append({Section::Unstaged, unstaged});
    return refs;
}

QString ChangesView::keyOf(const QTreeWidgetItem *item)
{
    if (const QVariant path = item->data(0, FileChangeDelegate::pathRole); path.isValid())
        return path.toString();
    // A folder by its path and a "/", apart from a file of that name.
    const QString folder = item->data(0, FileChangeDelegate::folderRole).toString();
    return folder.isEmpty() ? QString() : folder + QLatin1Char('/');
}

QStringList ChangesView::selectedKeys() const
{
    QStringList keys;
    for (const QTreeWidgetItem *item : fileList_->selectedItems())
        keys.append(keyOf(item));
    return keys;
}

void ChangesView::restoreSelection(const QStringList &selected, const std::optional<QString> &current)
{
    const QSignalBlocker blocker(fileList_);
    for (QTreeWidgetItemIterator it(fileList_); *it; ++it) {
        const QString key = keyOf(*it);
        if (selected.contains(key))
            (*it)->setSelected(true);
        if (current && key == *current)
            fileList_->setCurrentItem(*it, 0, QItemSelectionModel::NoUpdate);
    }
}

void ChangesView::selectFirstFileIfNone()
{
    if (!fileList_->selectedItems().isEmpty())
        return;
    const QList<QTreeWidgetItem *> rows = ChangeTree::filesUnder(rootItem());
    if (rows.isEmpty())
        return;
    const QSignalBlocker blocker(fileList_);
    fileList_->setCurrentItem(rows.first());
}

void ChangesView::selectNow(const QString &relativePath)
{
    if (!status_)
        return;
    const auto matches = [&relativePath](const QString &path) {
        return relativePath.isEmpty() || path == relativePath || path.startsWith(relativePath + QLatin1Char('/'));
    };
    for (QTreeWidgetItem *row : ChangeTree::filesUnder(rootItem())) {
        if (!matches(keyOf(row)))
            continue;
        fileList_->clearSelection();
        fileList_->setCurrentItem(row);
        fileList_->scrollToItem(row);
        return;
    }
}

void ChangesView::rebuildList()
{
    const QSignalBlocker blocker(fileList_);
    fileList_->clearSelection();
    QTreeWidgetItem *root = rootItem();
    qDeleteAll(root->takeChildren());
    if (!status_)
        return;
    // A row per path, for its staged changes, its unstaged ones, or both.
    QList<ListedFile> listed;
    for (const FileRef &ref : filesInListOrder()) {
        const QString &path = changeOf(ref).path;
        if (listed.isEmpty() || listed.last().path != path)
            listed.append({path});
        (ref.section == Section::Staged ? listed.last().staged : listed.last().unstaged) = ref.index;
    }
    QStringList paths;
    for (const ListedFile &file : std::as_const(listed))
        paths.append(file.path);
    const QList<QTreeWidgetItem *> rows = ChangeTree::addFiles(root, paths);
    for (int i = 0; i < listed.size(); ++i) {
        const ListedFile &file = listed[i];
        FileChangeDelegate::setChange(rows[i], shownChange(*status_, file));
        setStats(rows[i], *status_, file);
        rows[i]->setData(0, stagedIndexRole, file.staged);
        rows[i]->setData(0, unstagedIndexRole, file.unstaged);
        rows[i]->setCheckState(0, checkStateOf(file));
    }
    markFolders(root);
}

void ChangesView::markFolders(QTreeWidgetItem *parent)
{
    for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem *row = parent->child(i);
        if (!FileChangeDelegate::isFolder(row))
            continue;
        markFolders(row);
        if (collapsedFolders_.contains(keyOf(row)))
            row->setExpanded(false);
    }
}

int ChangesView::shownFileOf(QTreeWidgetItem *row) const
{
    const QList<QTreeWidgetItem *> rows = ChangeTree::filesUnder(row);
    if (rows.isEmpty())
        return -1;
    const QList<FileRef> refs = refsOf(rows.first());
    if (refs.isEmpty())
        return -1;
    return int(shownFiles_.indexOf(refs.first())); // a file's staged part comes first
}

void ChangesView::onCurrentItemChanged()
{
    QTreeWidgetItem *item = fileList_->currentItem();
    if (!item || !status_)
        return;
    const int file = shownFileOf(item);
    diff_->scrollToTop(file >= 0 ? diff_->fileStart(file) : 0);
}

void ChangesView::onSelectionChanged()
{
    showDiff();
    onCurrentItemChanged();
    updateActions();
}

void ChangesView::onItemChanged(QTreeWidgetItem *item)
{
    // A command still running, this one waits for it (GitCommands): staging
    // or unstaging whole files does not depend on what it changes.
    const QList<FileRef> refs = refsOf(item);
    if (refs.isEmpty() || !status_)
        return;
    if (item->checkState(0) == Qt::Checked)
        stageFiles(refs);
    else
        unstageFiles(refs);
}

void ChangesView::toggleSelectedFiles()
{
    const QList<FileRef> selection = selectedFiles();
    const bool hasUnstaged =
        std::ranges::any_of(selection, [](const FileRef &ref) { return ref.section == Section::Unstaged; });
    if (hasUnstaged)
        stageFiles(selection);
    else
        unstageFiles(selection);
}

std::optional<int> ChangesView::selectedFileIndex(const QList<QTreeWidgetItem *> &listedFiles) const
{
    const QList<QTreeWidgetItem *> selected = fileList_->selectedItems();
    if (selected.size() != 1 || FileChangeDelegate::isFolder(selected.first()))
        return std::nullopt;
    const qsizetype index = listedFiles.indexOf(selected.first());
    return index >= 0 ? std::optional(int(index)) : std::nullopt;
}

void ChangesView::selectFileAfter(int step)
{
    const QList<QTreeWidgetItem *> listedFiles = ChangeTree::filesUnder(rootItem());
    const std::optional<int> index = selectedFileIndex(listedFiles);
    if (!index)
        return;
    QTreeWidgetItem *row = listedFiles.value(*index + step);
    if (!row)
        return;
    fileList_->setCurrentItem(row);
    fileList_->scrollToItem(row);
}

void ChangesView::openItem(const QTreeWidgetItem *item)
{
    const QList<FileRef> refs = refsOf(item);
    if (refs.isEmpty() || !status_)
        return;
    const FileChange &change = changeOf(refs.last()); // on disk, when it changed there
    if (change.kind != ChangeKind::Deleted)
        emit openFileRequested(change.path, 0);
}

QList<ChangesView::FileRef> ChangesView::selectedFiles() const
{
    QList<FileRef> result;
    if (!status_)
        return result;
    // A folder stands for the files under it, which may be selected too.
    QSet<const QTreeWidgetItem *> listed;
    for (QTreeWidgetItem *item : fileList_->selectedItems()) {
        for (const QTreeWidgetItem *row : ChangeTree::filesUnder(item)) {
            if (!listed.contains(row)) {
                listed.insert(row);
                result.append(refsOf(row));
            }
        }
    }
    return result;
}

void ChangesView::showListMenu(const QPoint &position)
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
    const bool isOneFile = std::ranges::all_of(
        selection, [&](const FileRef &ref) { return changeOf(ref).path == changeOf(selection.first()).path; });
    if (isOneFile) {
        const FileChange &change = changeOf(selection.last()); // on disk, when it changed there
        if (change.kind != ChangeKind::Deleted) {
            menu.addSeparator();
            menu.addAction(tr("Open File"), this, [this, path = change.path] { emit openFileRequested(path, 0); });
        }
    }
    ContextMenus::exec(menu, *fileList_, fileList_->viewport()->mapToGlobal(position));
}
