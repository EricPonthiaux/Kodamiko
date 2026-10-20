// SPDX-License-Identifier: GPL-3.0-only

#include "FileTreeView.h"

#include "CommandPalette.h"
#include "EntryPopups.h"
#include "FileOperations.h"
#include "LastCommits.h"
#include "ProjectTreeModel.h"
#include "ui/FileIcons.h"
#include "ui/TreeColumnFit.h"
#include "ui/TreeMetrics.h"
#include "viewers/DiagramKinds.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace {

// At most this many folders stay pinned at the top, and in at most this
// share of the height (as VS Code).
constexpr int maximumStickyRows = 7;
constexpr qreal maximumStickyShare = 0.4;
// Clipboard marker of files cut in the tree: pasting them moves them.
const QString cutFormat = QStringLiteral("application/x-duet-cut");

bool isDarkPalette(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128;
}

// Whether moving `entry` into `destination` would put a folder inside itself.
bool isInsideItself(const QString &entry, const QString &destination)
{
    return FileOperations::isInside(destination, entry);
}

// The question asked before deleting a folder. Its files are counted up to
// a limit: a dependency folder may hold many.
QString deleteFolderQuestion(const QString &path)
{
    constexpr int countLimit = 1000;
    int files = 0;
    for (QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
         it.hasNext() && files <= countLimit; it.next())
        ++files;
    const QString name = QFileInfo(path).fileName();
    if (files == 0)
        return FileTreeView::tr("Delete the empty folder “%1”?").arg(name);
    if (files > countLimit)
        return FileTreeView::tr("Delete the folder “%1” and the more than %2 files it contains?")
            .arg(name)
            .arg(countLimit);
    return FileTreeView::tr("Delete the folder “%1” and the %n file(s) it contains?", nullptr, files).arg(name);
}

// Positions of the letters of `query` in `text`, as CommandPalette::matchScore
// finds them; empty if they do not all appear.
QList<qsizetype> matchedPositions(const QString &text, const QString &query)
{
    QList<qsizetype> positions;
    qsizetype position = 0;
    for (const QChar character : query) {
        if (character.isSpace())
            continue;
        const qsizetype found = text.indexOf(character, position, Qt::CaseInsensitive);
        if (found < 0)
            return {};
        positions.append(found);
        position = found + 1;
    }
    return positions;
}

// Space between the counts and letters at the end of a row.
constexpr int badgeSpacing = 6;

} // namespace

// Highlights, during a speed search, the letters that match in each name.
class FileTreeDelegate : public QStyledItemDelegate {
public:
    explicit FileTreeDelegate(FileTreeView *view)
        : QStyledItemDelegate(view)
        , view_(view)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), treeRowHeight(option.font)));
        return size;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        if (index.column() != 0)
            return;
        paintBadges(painter, option, index);
        if (view_->speedSearch_.isEmpty())
            return;
        const QString name = index.data(Qt::DisplayRole).toString();
        const QList<qsizetype> positions = matchedPositions(name, view_->speedSearch_);
        if (positions.isEmpty())
            return;
        QStyleOptionViewItem item = option;
        initStyleOption(&item, index);
        const QStyle *style = item.widget ? item.widget->style() : QApplication::style();
        const QRect text = style->subElementRect(QStyle::SE_ItemViewItemText, &item, item.widget);
        const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, item.widget) + 1;
        const QFontMetrics metrics(item.font);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        const bool dark = isDarkPalette(option.palette);
        QColor color = view_->searchHighlight_;
        color.setAlpha(dark ? 90 : 110);
        painter->setBrush(color);
        for (const qsizetype position : positions) {
            const int x = text.left() + margin + metrics.horizontalAdvance(name.left(position));
            const int width = metrics.horizontalAdvance(name.at(position));
            painter->drawRoundedRect(QRectF(x - 0.5, text.top() + 1, width + 1, text.height() - 2), 2, 2);
        }
        painter->restore();
    }

private:
    // At the right of the visible part of the row, if the name leaves room
    // for them: the git letter, and the count of problems before it.
    void paintBadges(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        QList<std::pair<QString, QColor>> badges; // from the right
        if (const QString letter = index.data(TreeRoles::statusLetter).toString(); !letter.isEmpty())
            badges.append({letter, index.data(TreeRoles::statusColor).value<QColor>()});
        if (const int count = index.data(TreeRoles::problemCount).toInt(); count > 0)
            badges.append({QString::number(count), index.data(TreeRoles::problemColor).value<QColor>()});
        if (badges.isEmpty())
            return;
        QStyleOptionViewItem item = option;
        initStyleOption(&item, index);
        const QStyle *style = item.widget ? item.widget->style() : QApplication::style();
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &item, item.widget);
        const QFontMetrics metrics(item.font);
        const int nameEnd = textRect.left() + metrics.horizontalAdvance(item.text) + 8;
        // The column is as wide as the names: the badges go at the view's edge.
        int right = view_->viewport()->width() - 10;
        const bool selected = (option.state & QStyle::State_Selected) && (option.state & QStyle::State_Active);
        painter->save();
        painter->setFont(item.font);
        for (const auto &[badge, color] : badges) {
            const int width = metrics.horizontalAdvance(badge);
            if (right - width < nameEnd)
                break;
            painter->setPen(selected ? QColor(Qt::white) : color);
            painter->drawText(QRect(right - width, option.rect.top(), width, option.rect.height()),
                              Qt::AlignRight | Qt::AlignVCenter, badge);
            right -= width + badgeSpacing;
        }
        painter->restore();
    }

    FileTreeView *view_;
};

FileTreeView::FileTreeView(QWidget *parent)
    : QTreeView(parent)
    , model_(new ProjectTreeModel(this))
    , proxy_(new TreeDecorations(this))
    , lastCommits_(new LastCommits(this))
{
    proxy_->setSourceModel(model_);
    setModel(proxy_);
    header()->hide();
    setAnimated(false);
    setUniformRowHeights(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);

    new TreeColumnFit(this);
    setItemDelegate(new FileTreeDelegate(this));

    speedSearchLabel_ = new QLabel(viewport());
    speedSearchLabel_->setAutoFillBackground(true);
    speedSearchLabel_->setMargin(3);
    speedSearchLabel_->hide();

    connect(proxy_, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &insertedParent) {
        if (scope_ != Scope::Project)
            expandScope(insertedParent);
        expandPending();
        selectPending();
    });
    connect(model_, &ProjectTreeModel::directoryLoaded, this, &FileTreeView::expandPending);

    // Drag and drop: moves within the tree (copies with Ctrl), copies
    // from elsewhere (a file manager).
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &FileTreeView::showContextMenu);

    // A click without Ctrl or Shift, which extend the selection.
    connect(this, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        const bool extendsSelection = QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
        const QModelIndex source = proxy_->mapToSource(index);
        if (!extendsSelection && !model_->isDir(source))
            emit filePreviewRequested(model_->filePath(source));
    });
    connect(lastCommits_, &LastCommits::found, this, &FileTreeView::showLastCommit);
    connect(this, &QTreeView::activated, this, [this](const QModelIndex &index) {
        const QModelIndex source = proxy_->mapToSource(index);
        if (!model_->isDir(source))
            emit fileActivated(model_->filePath(source));
        setSpeedSearch({});
    });
}

FileTreeView::~FileTreeView() = default;

void FileTreeView::setRootPath(const QString &path)
{
    rootPath_ = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    pendingExpansions_.clear();
    pendingSelection_.clear();
    proxy_->setRootPath(rootPath_);
    proxy_->setStatuses(statuses_);
    proxy_->setCut({});
    projectExpanded_.clear();
    undoStack_ = {};
    lastCommits_->setFolder(rootPath_);
    // The previous folder's layout is not restored.
    filterExpanded_.reset();
    proxy_->setNameFilter({});
    setSpeedSearch({});
    model_->setRootPath(rootPath_);
    updateScopeFiles();
    expandRoot();
}

void FileTreeView::expandRoot()
{
    if (const QModelIndex root = proxy_->index(0, 0); root.isValid())
        expand(root);
}

ProjectTreeModel::Options FileTreeView::treeOptions() const
{
    return model_->options();
}

void FileTreeView::setTreeOptions(const ProjectTreeModel::Options &options)
{
    if (options == model_->options())
        return;
    // The model is rebuilt: keep what is expanded and selected.
    QStringList expanded;
    collectExpanded(QModelIndex(), &expanded);
    const QString current = currentPath();
    model_->setOptions(options);
    updateScopeFiles();
    expandRoot();
    if (scope_ == Scope::Project)
        restoreExpandedPaths(expanded);
    if (!current.isEmpty())
        select(current);
}

void FileTreeView::setDarkIcons(bool dark)
{
    model_->setDarkIcons(dark);
    viewport()->update();
}

void FileTreeView::refreshFromDisk()
{
    model_->refresh();
}

QStringList FileTreeView::expandedPaths() const
{
    if (scope_ != Scope::Project)
        return projectExpanded_;
    QStringList paths;
    collectExpanded(QModelIndex(), &paths);
    return paths;
}

void FileTreeView::collectExpanded(const QModelIndex &parent, QStringList *paths) const
{
    for (int row = 0; row < proxy_->rowCount(parent); ++row) {
        const QModelIndex index = proxy_->index(row, 0, parent);
        if (!isExpanded(index))
            continue;
        paths->append(pathOf(index));
        collectExpanded(index, paths);
    }
}

void FileTreeView::restoreExpandedPaths(const QStringList &paths)
{
    pendingExpansions_.clear();
    for (const QString &path : paths) {
        if (QFileInfo(path).isDir())
            pendingExpansions_.append(path);
    }
    // Parents first: expanding a folder triggers reading its contents.
    std::sort(pendingExpansions_.begin(), pendingExpansions_.end(),
              [](const QString &a, const QString &b) { return a.size() < b.size(); });
    expandPending();
}

void FileTreeView::expandPending()
{
    // expand() may re-emit rowsInserted, and so call us again: work on a copy.
    const QStringList pending = std::exchange(pendingExpansions_, {});
    QStringList remaining;
    for (const QString &path : pending) {
        const QModelIndex index = proxy_->mapFromSource(model_->index(path));
        if (index.isValid())
            expand(index);
        else
            remaining.append(path);
    }
    pendingExpansions_ += remaining;
}

void FileTreeView::createFile()
{
    createEntry(targetFolder(currentIndex()), false);
}

void FileTreeView::createFolder()
{
    createEntry(targetFolder(currentIndex()), true);
}

void FileTreeView::createDiagram(const QString &kindId)
{
    createDiagramIn(targetFolder(currentIndex()), kindId);
}

void FileTreeView::generateDiagram()
{
    if (!rootPath_.isEmpty())
        emit generateDiagramRequested(targetFolder(currentIndex()));
}

void FileTreeView::showContextMenu(const QPoint &position)
{
    if (rootPath_.isEmpty())
        return;
    const QModelIndex index = indexAt(position);
    // A click in the selection acts on all of it.
    if (index.isValid() && !selectionModel()->isSelected(index))
        setCurrentIndex(index);
    else if (index.isValid())
        selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    const QString folder = targetFolder(index);
    const QString clicked = index.isValid() ? pathOf(index) : rootPath_;
    const QStringList targets = index.isValid() ? selectedPaths() : QStringList{rootPath_};

    QMenu menu;
    menu.addAction(tr("New &File…"), this, [this, folder] { createEntry(folder, false); });
    menu.addAction(tr("New F&older…"), this, [this, folder] { createEntry(folder, true); });
    if (const QList<DiagramKind> kinds = DiagramKinds::available(); !kinds.isEmpty()) {
        QMenu *diagram = menu.addMenu(tr("New &Diagram"));
        for (const DiagramKind &kind : kinds)
            diagram->addAction(kind.title + QStringLiteral("…"), this,
                               [this, folder, id = kind.id] { createDiagramIn(folder, id); });
        diagram->addSeparator();
        diagram->addAction(tr("Drawn by &Claude…"), this, [this, folder] { emit generateDiagramRequested(folder); });
    }
    menu.addSeparator();
    addEntryActions(&menu, index);

    menu.addSeparator();
    QMenu *copy = menu.addMenu(tr("Copy Path/&Reference"));
    copy->addAction(tr("&Absolute Path"), QKeySequence(tr("Ctrl+Shift+C")), this,
                    [this, clicked] { copyPath(clicked, false); });
    copy->addAction(tr("Path From &Project Root"), QKeySequence(tr("Ctrl+Alt+Shift+C")), this,
                    [this, clicked] { copyPath(clicked, true); });
    copy->addAction(tr("File &Name"), this,
                    [clicked] { QApplication::clipboard()->setText(QFileInfo(clicked).fileName()); });
    menu.addAction(tr("Send to &Claude"), QKeySequence(tr("Ctrl+Alt+K")), this, [this, targets] {
        for (const QString &target : targets)
            emit sendToClaudeRequested(target);
    });
    QMenu *open = menu.addMenu(tr("Open &In"));
    open->addAction(tr("&Terminal"), this, [this, folder] { emit openTerminalRequested(folder); });
    open->addAction(tr("&File Manager"), this, [folder] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
    if (inRepository_)
        addGitMenu(&menu, clicked, targets);
    menu.exec(viewport()->mapToGlobal(position));
}

// Clipboard, undo, rename and delete.
void FileTreeView::addEntryActions(QMenu *menu, const QModelIndex &index)
{
    const QStringList selection = index.isValid() ? editableSelection() : QStringList();
    menu->addAction(tr("Cu&t"), QKeySequence::Cut, this, &FileTreeView::cutSelection)->setEnabled(!selection.isEmpty());
    menu->addAction(tr("&Copy"), QKeySequence::Copy, this, &FileTreeView::copySelection)
        ->setEnabled(!selection.isEmpty());
    menu->addAction(tr("&Paste"), QKeySequence::Paste, this, &FileTreeView::paste)
        ->setEnabled(!localPaths(QApplication::clipboard()->mimeData()).isEmpty());
    if (const QString undoText = undoStack_.lastDescription(); !undoText.isEmpty())
        menu->addAction(undoText, QKeySequence::Undo, this, &FileTreeView::undo);
    if (selection.isEmpty())
        return;
    menu->addSeparator();
    if (selection.size() == 1) {
        menu->addAction(tr("Cop&y…"), QKeySequence(Qt::Key_F5), this, &FileTreeView::duplicateCurrent);
        const QString &path = selection.first();
        menu->addAction(tr("&Rename…"), QKeySequence(Qt::Key_F2), this, [this, path] { renameEntry(path); });
    }
    menu->addAction(tr("&Delete…"), QKeySequence::Delete, this, [this, selection] { deleteEntries(selection); });
}

void FileTreeView::addGitMenu(QMenu *menu, const QString &path, const QStringList &selection)
{
    const bool isDir = QFileInfo(path).isDir();
    const std::optional<FileStatus> status = proxy_->statusOf(path, isDir);
    const bool changed = status && *status != FileStatus::Ignored;
    // Rollback restores the last commit: nothing to restore for a file git does not know.
    const bool trackedChange = changed && *status != FileStatus::Untracked;
    QMenu *git = menu->addMenu(tr("&Git"));
    git->addAction(tr("Show &Diff"), this, [this, path] { emit showDiffRequested(path); })->setEnabled(changed);
    git->addAction(tr("&Add"), this,
                   [this, selection] {
                       for (const QString &entry : selection)
                           emit stageRequested(entry);
                   })
        ->setEnabled(changed);
    git->addAction(tr("&Rollback…"), this, [this, path] { confirmRollback(path); })->setEnabled(trackedChange);
    git->addSeparator();
    git->addAction(tr("Show &History"), this, [this, path] { emit historyRequested(path); })
        ->setEnabled(!status || *status != FileStatus::Untracked);
}

void FileTreeView::confirmRollback(const QString &path)
{
    const QFileInfo info(path);
    const QString relative = QDir(QFileInfo(rootPath_).absolutePath()).relativeFilePath(info.absolutePath());
    const QString message = (info.isDir() ? tr("Restore the files of “%1” to their state in the last commit?")
                                          : tr("Restore “%1” to its state in the last commit?"))
                                .arg(path == rootPath_ ? QDir::toNativeSeparators(rootPath_) : info.fileName()) +
                            QLatin1Char('\n') +
                            tr("A snapshot is taken first: Git ▸ Restore a Snapshot brings the changes back.");
    auto *popup = new ConfirmPopup(
        {tr("Rollback"), QDir::toNativeSeparators(relative + QLatin1Char('/')), message, tr("Rollback")},
        [this, path](ConfirmPopup *) {
            emit rollbackRequested(path);
            return true;
        },
        this);
    popup->popup();
}

void FileTreeView::copyPath(const QString &path, bool relative)
{
    QString text = path;
    if (relative) {
        text = QDir(rootPath_).relativeFilePath(path);
        if (text == QLatin1String("."))
            text = QFileInfo(rootPath_).fileName();
    }
    QApplication::clipboard()->setText(QDir::toNativeSeparators(text));
}

void FileTreeView::renameCurrent()
{
    if (const QString path = editablePath(currentIndex()); !path.isEmpty())
        renameEntry(path);
}

void FileTreeView::deleteSelection()
{
    if (const QStringList paths = editableSelection(); !paths.isEmpty())
        deleteEntries(paths);
}

void FileTreeView::keyPressEvent(QKeyEvent *event)
{
    if (handleSpeedSearchKey(event))
        return;
    if (handlePathKey(event))
        return;
    if (handleEntryKey(event))
        return;
    QTreeView::keyPressEvent(event);
}

bool FileTreeView::handlePathKey(QKeyEvent *event)
{
    const QString current = currentIndex().isValid() ? pathOf(currentIndex()) : rootPath_;
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (event->key() == Qt::Key_C && modifiers == (Qt::ControlModifier | Qt::ShiftModifier)) {
        copyPath(current, false);
        return true;
    }
    if (event->key() == Qt::Key_C && modifiers == (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) {
        copyPath(current, true);
        return true;
    }
    if (event->key() == Qt::Key_K && modifiers == (Qt::ControlModifier | Qt::AltModifier)) {
        const QStringList selection = currentIndex().isValid() ? selectedPaths() : QStringList{rootPath_};
        for (const QString &path : selection)
            emit sendToClaudeRequested(path);
        return true;
    }
    return false;
}

bool FileTreeView::handleEntryKey(QKeyEvent *event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    const bool isF2 = event->key() == Qt::Key_F2 && modifiers == Qt::NoModifier;
    const bool isShiftF6 = event->key() == Qt::Key_F6 && event->modifiers() == Qt::ShiftModifier; // IntelliJ's
    if (event->matches(QKeySequence::Cut))
        cutSelection();
    else if (event->matches(QKeySequence::Copy))
        copySelection();
    else if (event->matches(QKeySequence::Paste))
        paste();
    else if (event->matches(QKeySequence::Undo))
        undo();
    else if (event->key() == Qt::Key_F5 && modifiers == Qt::NoModifier)
        duplicateCurrent();
    else if (event->matches(QKeySequence::Delete))
        deleteSelection();
    else if (isF2 || isShiftF6)
        renameCurrent();
    else
        return false;
    return true;
}

QString FileTreeView::editablePath(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};
    const QString path = pathOf(index);
    return path == rootPath_ || !path.startsWith(rootPath_ + QLatin1Char('/')) ? QString() : path;
}

QStringList FileTreeView::editableSelection() const
{
    QStringList paths = selectedPaths();
    paths.removeIf([this](const QString &path) { return !path.startsWith(rootPath_ + QLatin1Char('/')); });
    return paths;
}

// The clicked folder, or the folder containing the clicked file.
QString FileTreeView::targetFolder(const QModelIndex &index) const
{
    if (!index.isValid())
        return rootPath_;
    const QModelIndex source = proxy_->mapToSource(index);
    const QString path = model_->filePath(source);
    // A nested file's folder is that of its main file: the path's.
    return model_->isDir(source) ? path : QFileInfo(path).absolutePath();
}

void FileTreeView::createEntry(const QString &folder, bool isFolder)
{
    if (folder.isEmpty())
        return;
    const QString relative = QDir(QFileInfo(rootPath_).absolutePath()).relativeFilePath(folder);
    auto *popup = new NamePopup(
        isFolder ? tr("New Folder") : tr("New File"), QDir::toNativeSeparators(relative + QLatin1Char('/')),
        [this, folder, isFolder](const QString &name) { return createEntryNamed(folder, name, isFolder); }, this);
    popup->popup();
}

void FileTreeView::createDiagramIn(const QString &folder, const QString &kindId)
{
    const DiagramKind kind = DiagramKinds::find(kindId);
    if (folder.isEmpty() || kind.id.isEmpty())
        return;
    const QString relative = QDir(QFileInfo(rootPath_).absolutePath()).relativeFilePath(folder);
    auto *popup = new NamePopup(
        tr("New %1").arg(kind.title), QDir::toNativeSeparators(relative + QLatin1Char('/')),
        [this, folder, kind](const QString &name) {
            if (name.isEmpty())
                return tr("Enter a name");
            return createEntryNamed(folder, DiagramKinds::withExtension(name, kind), false, kind.blank);
        },
        this);
    popup->popup();
}

QString FileTreeView::createEntryNamed(const QString &folder,
                                       const QString &name,
                                       bool isFolder,
                                       const QByteArray &content)
{
    const EntryTarget target = resolveNewEntry(rootPath_, folder, name, isFolder);
    if (!target.error.isEmpty())
        return target.error;
    const QString &path = target.path;
    const QString parent = isFolder ? path : QFileInfo(path).absolutePath();
    if (!QDir().mkpath(parent))
        return tr("Cannot create the folder “%1”").arg(QDir::toNativeSeparators(parent));
    if (!isFolder) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(content) != content.size())
            return tr("Cannot create “%1”: %2").arg(QDir::toNativeSeparators(path), file.errorString());
    }

    undoStack_.record({.kind = FileUndoStack::Step::Kind::Created, .path = path, .source = {}});
    reveal(path);
    emit entryCreated(path);
    if (!isFolder)
        emit fileActivated(path);
    return {};
}

void FileTreeView::renameEntry(const QString &path)
{
    const QFileInfo info(path);
    const bool isFolder = info.isDir() && !info.isSymLink();
    const QString relative = QDir(QFileInfo(rootPath_).absolutePath()).relativeFilePath(info.absolutePath());
    auto *popup = new NamePopup(
        isFolder ? tr("Rename Folder") : tr("Rename File"), QDir::toNativeSeparators(relative + QLatin1Char('/')),
        [this, path](const QString &name) { return renameEntryTo(path, name); }, this);
    popup->setName(info.fileName(), isFolder);
    popup->popup();
}

QString FileTreeView::renameEntryTo(const QString &path, const QString &name)
{
    const EntryTarget target = resolveRename(path, name);
    if (!target.error.isEmpty())
        return target.error;
    if (target.path == path)
        return {};
    if (hasUnsavedChanges_ && hasUnsavedChanges_(path)) {
        return QFileInfo(path).isDir() ? tr("Files in this folder have unsaved changes: save them first")
                                       : tr("The file has unsaved changes: save it first");
    }
    if (!QDir().rename(path, target.path))
        return tr("Cannot rename “%1”").arg(QFileInfo(path).fileName());

    // Keep the expanded folders inside a renamed folder.
    QStringList expanded = expandedPaths();
    for (QString &folder : expanded) {
        if (folder == path || folder.startsWith(path + QLatin1Char('/')))
            folder = target.path + folder.mid(path.size());
    }
    restoreExpandedPaths(expanded);
    undoStack_.record({.kind = FileUndoStack::Step::Kind::Moved, .path = target.path, .source = path});
    reveal(target.path);
    emit entryRenamed(path, target.path);
    return {};
}

void FileTreeView::deleteEntry(const QString &path)
{
    const QFileInfo info(path);
    const bool isFolder = info.isDir() && !info.isSymLink();
    QString message = isFolder ? deleteFolderQuestion(path) : tr("Delete “%1”?").arg(info.fileName());
    message += QLatin1Char('\n') + tr("It will be moved to the trash.");
    if (hasUnsavedChanges_ && hasUnsavedChanges_(path))
        message += QLatin1Char(' ') + tr("Unsaved changes in the editor will be lost.");

    const QString relative = QDir(QFileInfo(rootPath_).absolutePath()).relativeFilePath(info.absolutePath());
    auto *popup = new ConfirmPopup(
        {isFolder ? tr("Delete Folder") : tr("Delete File"), QDir::toNativeSeparators(relative + QLatin1Char('/')),
         message, tr("Delete")},
        [this, path, isFolder, permanently = false](ConfirmPopup *confirm) mutable {
            if (permanently)
                return deletePermanently(confirm, path, isFolder);
            if (moveToTrash(path))
                return true;
            permanently = true;
            confirm->setMessage(tr("“%1” cannot be moved to the trash. Delete it permanently? "
                                   "This cannot be undone.")
                                    .arg(QFileInfo(path).fileName()),
                                tr("Delete Permanently"));
            return false;
        },
        this);
    popup->popup();
}

void FileTreeView::deleteEntries(const QStringList &paths)
{
    if (paths.size() == 1) {
        deleteEntry(paths.first());
        return;
    }
    QString message = tr("Delete these %n items?", nullptr, int(paths.size())) + QLatin1Char('\n') +
                      tr("They will be moved to the trash.");
    const bool hasUnsaved = hasUnsavedChanges_ && std::ranges::any_of(paths, hasUnsavedChanges_);
    if (hasUnsaved)
        message += QLatin1Char(' ') + tr("Unsaved changes in the editor will be lost.");
    const QString relative = QDir(QFileInfo(rootPath_).absolutePath()).relativeFilePath(rootPath_);
    auto *popup = new ConfirmPopup(
        {tr("Delete"), QDir::toNativeSeparators(relative + QLatin1Char('/')), message, tr("Delete")},
        [this, paths](ConfirmPopup *) {
            QStringList kept;
            for (const QString &path : paths) {
                if (!moveToTrash(path))
                    kept.append(QFileInfo(path).fileName());
            }
            if (!kept.isEmpty())
                emit statusMessage(tr("Cannot move to the trash: %1").arg(kept.join(QStringLiteral(", "))));
            return true;
        },
        this);
    popup->popup();
}

bool FileTreeView::moveToTrash(const QString &path)
{
    QString pathInTrash;
    if (!QFile::moveToTrash(path, &pathInTrash))
        return false;
    // Where the system does not say, the entry cannot be taken back.
    if (!pathInTrash.isEmpty())
        undoStack_.record({.kind = FileUndoStack::Step::Kind::Trashed, .path = path, .source = pathInTrash});
    emit entryDeleted(path);
    return true;
}

bool FileTreeView::deletePermanently(ConfirmPopup *confirm, const QString &path, bool isFolder)
{
    const bool removed = isFolder ? QDir(path).removeRecursively() : QFile::remove(path);
    if (!removed) {
        confirm->showError(tr("Cannot delete “%1”").arg(QFileInfo(path).fileName()));
        return false;
    }
    emit entryDeleted(path);
    return true;
}

// Expands the folders leading to `path` and selects it, once the model has
// read it.
void FileTreeView::reveal(const QString &path)
{
    pendingSelection_ = path;
    selectPending();
}

void FileTreeView::selectPending()
{
    if (pendingSelection_.isEmpty())
        return;
    // The model reads the folders leading to it.
    const QModelIndex index = proxy_->mapFromSource(model_->index(pendingSelection_));
    if (!index.isValid())
        return;
    pendingSelection_.clear();
    // Folders, compact chains and main files of a nested file.
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
        expand(parent);
    setCurrentIndex(index);
    scrollTo(index);
}

QString FileTreeView::pathOf(const QModelIndex &index) const
{
    return model_->filePath(proxy_->mapToSource(index));
}

QString FileTreeView::currentPath() const
{
    return currentIndex().isValid() ? pathOf(currentIndex()) : QString();
}

QStringList FileTreeView::selectedPaths() const
{
    QStringList paths;
    for (const QModelIndex &index : selectionModel()->selectedRows())
        paths.append(pathOf(index));
    if (paths.isEmpty() && currentIndex().isValid())
        paths.append(pathOf(currentIndex()));
    // An entry inside a selected folder goes with it.
    QStringList topmost;
    for (const QString &path : paths) {
        const bool isInSelectedFolder = std::ranges::any_of(
            paths, [&path](const QString &other) { return other != path && FileOperations::isInside(path, other); });
        if (!isInSelectedFolder)
            topmost.append(path);
    }
    return topmost;
}

bool FileTreeView::select(const QString &path)
{
    const QString cleanPath = QDir::cleanPath(path);
    if (rootPath_.isEmpty() || !(cleanPath == rootPath_ || cleanPath.startsWith(rootPath_ + QLatin1Char('/'))))
        return false;
    if (scope_ == Scope::ChangedFiles && statuses_.value(cleanPath, FileStatus::Ignored) == FileStatus::Ignored)
        return false;
    if (scope_ == Scope::EditedByClaude && !claudeEdited_.contains(cleanPath))
        return false;
    reveal(cleanPath);
    return true;
}

void FileTreeView::setStatuses(const FileStatusMap &statuses, bool inRepository)
{
    statuses_ = statuses;
    inRepository_ = inRepository;
    proxy_->setStatuses(statuses);
    if (scope_ == Scope::ChangedFiles)
        updateScopeFiles();
    viewport()->update();
}

void FileTreeView::setClaudeTouched(const QSet<QString> &paths)
{
    proxy_->setTouched(paths);
    viewport()->update();
}

void FileTreeView::setProblems(const ProblemCounts &problems)
{
    proxy_->setProblems(problems);
    viewport()->update();
}

void FileTreeView::setOpenFiles(const QSet<QString> &paths)
{
    proxy_->setOpenFiles(paths);
    viewport()->update();
}

void FileTreeView::setClaudeEdited(const QStringList &paths)
{
    claudeEdited_ = paths;
    if (scope_ == Scope::EditedByClaude)
        updateScopeFiles();
}

void FileTreeView::setScope(Scope scope)
{
    if (scope == scope_)
        return;
    if (scope_ == Scope::Project)
        collectExpanded(QModelIndex(), &projectExpanded_);
    scope_ = scope;
    setSpeedSearch({});
    updateScopeFiles();
    if (scope_ == Scope::Project) {
        collapseAll();
        if (const QModelIndex root = proxy_->index(0, 0, rootIndex()); root.isValid())
            expand(root); // the working folder
        restoreExpandedPaths(std::exchange(projectExpanded_, {}));
    }
}

void FileTreeView::updateScopeFiles()
{
    if (scope_ == Scope::Project) {
        proxy_->setScope(std::nullopt, {});
        return;
    }
    QSet<QString> files;
    QSet<QString> ancestors;
    for (const QString &path : scopeCandidates()) {
        const QModelIndex index = rowInTree(path);
        if (!index.isValid())
            continue;
        files.insert(path);
        for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
            ancestors.insert(model_->filePath(parent));
    }
    proxy_->setScope(files, ancestors);
    expandScope(rootIndex());
}

QStringList FileTreeView::scopeCandidates() const
{
    if (scope_ != Scope::ChangedFiles)
        return claudeEdited_;
    QStringList paths;
    for (auto it = statuses_.cbegin(); it != statuses_.cend(); ++it) {
        if (it.value() != FileStatus::Ignored)
            paths.append(it.key());
    }
    return paths;
}

QModelIndex FileTreeView::rowInTree(const QString &path) const
{
    if (!path.startsWith(rootPath_ + QLatin1Char('/')) || !QFileInfo::exists(path))
        return {};
    // The model reads the folders leading to it; its rows lead the way.
    return model_->index(path);
}

// A scope shows few files: everything is expanded.
void FileTreeView::expandScope(const QModelIndex &parent)
{
    for (int row = 0; row < proxy_->rowCount(parent); ++row) {
        const QModelIndex index = proxy_->index(row, 0, parent);
        if (proxy_->hasChildren(index)) { // folders, and main files of nested ones
            expand(index);
            expandScope(index);
        }
    }
}

bool FileTreeView::speedSearchMatches(const QModelIndex &index) const
{
    return CommandPalette::matchScore(index.data(Qt::DisplayRole).toString(), speedSearch_) >= 0;
}

QModelIndex FileTreeView::findMatch(const QModelIndex &from, bool forward) const
{
    for (QModelIndex index = from; index.isValid(); index = forward ? indexBelow(index) : indexAbove(index)) {
        if (speedSearchMatches(index))
            return index;
    }
    return {};
}

void FileTreeView::setFilterWhileTyping(bool enabled)
{
    if (enabled == filterWhileTyping_)
        return;
    setSpeedSearch({});
    filterWhileTyping_ = enabled;
}

void FileTreeView::applyNameFilter(const QString &text)
{
    if (!text.isEmpty() && !filterExpanded_) {
        QStringList expanded;
        collectExpanded(QModelIndex(), &expanded);
        filterExpanded_ = expanded;
    }
    proxy_->setNameFilter(text);
    if (!text.isEmpty()) {
        expandScope(rootIndex()); // the matches in sight
        return;
    }
    const std::optional<QStringList> expanded = std::exchange(filterExpanded_, std::nullopt);
    if (!expanded)
        return;
    // Back as it was, on the item chosen.
    const QString current = currentPath();
    collapseAll();
    restoreExpandedPaths(*expanded);
    if (scope_ != Scope::Project)
        expandScope(rootIndex());
    if (!current.isEmpty())
        reveal(current);
}

void FileTreeView::setSpeedSearch(const QString &text)
{
    speedSearch_ = text;
    if (filterWhileTyping_)
        applyNameFilter(text);
    if (text.isEmpty()) {
        speedSearchLabel_->hide();
        viewport()->update();
        return;
    }
    const QModelIndex start = currentIndex().isValid() ? currentIndex() : proxy_->index(0, 0, rootIndex());
    QModelIndex match = findMatch(start, true);
    if (!match.isValid())
        match = findMatch(proxy_->index(0, 0, rootIndex()), true); // wraps around
    const bool dark = isDarkPalette(palette());
    QPalette labelPalette = palette();
    labelPalette.setColor(QPalette::Window, dark ? QColor("#3c3f41") : QColor("#f7f7f7"));
    labelPalette.setColor(QPalette::WindowText,
                          match.isValid() ? palette().color(QPalette::Text) : QColor(dark ? "#e06c75" : "#e45649"));
    speedSearchLabel_->setPalette(labelPalette);
    speedSearchLabel_->setText(filterWhileTyping_ ? tr("Filter: %1").arg(text) : tr("Search for: %1").arg(text));
    speedSearchLabel_->adjustSize();
    speedSearchLabel_->move(0, 0);
    speedSearchLabel_->show();
    speedSearchLabel_->raise();
    if (match.isValid()) {
        setCurrentIndex(match);
        scrollTo(match);
    }
    viewport()->update();
}

bool FileTreeView::handleSpeedSearchKey(QKeyEvent *event)
{
    if (!speedSearch_.isEmpty()) {
        if (const std::optional<bool> consumed = handleActiveSearchKey(event))
            return *consumed;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
    const QString text = event->text();
    if (modifiers != Qt::NoModifier || text.isEmpty() || !text.at(0).isPrint())
        return false;
    if (speedSearch_.isEmpty() && text.at(0).isSpace())
        return false;
    setSpeedSearch(speedSearch_ + text);
    return true;
}

std::optional<bool> FileTreeView::handleActiveSearchKey(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        setSpeedSearch({});
        return true;
    case Qt::Key_Backspace:
        setSpeedSearch(speedSearch_.chopped(1));
        return true;
    case Qt::Key_Up:
    case Qt::Key_Down:
        stepSpeedSearch(event->key() == Qt::Key_Down);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Tab:
        setSpeedSearch({});
        return false; // usual meaning, too
    default:
        return std::nullopt;
    }
}

void FileTreeView::stepSpeedSearch(bool forward)
{
    const QModelIndex current = currentIndex();
    const QModelIndex match = findMatch(forward ? indexBelow(current) : indexAbove(current), forward);
    if (!match.isValid())
        return;
    setCurrentIndex(match);
    scrollTo(match);
}

void FileTreeView::focusOutEvent(QFocusEvent *event)
{
    // A popup (context menu, rename…) keeps the search; anything else ends it.
    if (event->reason() != Qt::PopupFocusReason)
        setSpeedSearch({});
    QTreeView::focusOutEvent(event);
}

void FileTreeView::mousePressEvent(QMouseEvent *event)
{
    // A filter stays while its results are clicked.
    if (!filterExpanded_)
        setSpeedSearch({});
    // A pinned folder: back to it.
    const int rowHeight = stickyRowHeight();
    const int row = rowHeight > 0 ? event->position().toPoint().y() / rowHeight : -1;
    if (row >= 0 && row < stickyRows_.size() && event->button() == Qt::LeftButton) {
        const QModelIndex folder = stickyRows_.at(row);
        if (folder.isValid()) {
            scrollTo(folder, QAbstractItemView::PositionAtTop);
            setCurrentIndex(folder);
        }
        return;
    }
    QTreeView::mousePressEvent(event);
}

int FileTreeView::stickyRowHeight() const
{
    return rowHeight(model()->index(0, 0, rootIndex()));
}

QList<QPersistentModelIndex> FileTreeView::computeStickyRows() const
{
    // As VS Code: the expanded folders whose rows went above the top while
    // their contents are still in view, outermost first. The working folder
    // is left out: it holds everything.
    QList<QPersistentModelIndex> rows;
    const int height = stickyRowHeight();
    if (height <= 0)
        return rows;
    const int limit = std::min(maximumStickyRows, int(viewport()->height() * maximumStickyShare / height));
    const QModelIndex workingFolder = model()->index(0, 0, rootIndex());
    while (rows.size() < limit) {
        // The first row below the pinned ones, and the folders around it.
        const QModelIndex below = indexAt(QPoint(1, int(rows.size()) * height + 1));
        if (!below.isValid())
            break;
        QList<QModelIndex> chain;
        for (QModelIndex folder = below.parent(); folder.isValid() && folder != workingFolder && folder != rootIndex();
             folder = folder.parent())
            chain.prepend(folder);
        // A folder whose own row is cut by the top is pinned too.
        if (below != workingFolder && isExpanded(below) && model()->hasChildren(below) &&
            visualRect(below).top() < int(rows.size()) * height)
            chain.append(below);
        if (chain.size() <= rows.size())
            break;
        rows.append(chain.at(rows.size()));
    }
    return rows;
}

void FileTreeView::paintEvent(QPaintEvent *event)
{
    QTreeView::paintEvent(event);
    stickyRows_ = computeStickyRows();
    if (stickyRows_.isEmpty())
        return;
    const int height = stickyRowHeight();
    QPainter painter(viewport());
    const QColor background = palette().color(QPalette::Base);
    QStyleOptionViewItem option;
    initViewItemOption(&option);
    for (int i = 0; i < stickyRows_.size(); ++i) {
        const QModelIndex folder = stickyRows_.at(i);
        const QRect row(0, i * height, viewport()->width(), height);
        painter.fillRect(row, background);
        const QRect item = visualRect(folder);
        // Its guides and open arrow, then its icon and name, where they would
        // be. (QTreeView::drawBranches only draws the row being painted.)
        const QRect branches(0, row.top(), item.left(), height);
        drawIndentGuides(&painter, branches, folder);
        // As QTreeView builds it for a branch.
        QStyleOptionViewItem arrow;
        initViewItemOption(&arrow);
        arrow.rect = QRect(item.left() - indentation(), row.top(), indentation(), height);
        arrow.state = QStyle::State_Enabled | QStyle::State_Item | QStyle::State_Children | QStyle::State_Open;
        style()->drawPrimitive(QStyle::PE_IndicatorBranch, &arrow, &painter, this);
        option.rect = QRect(item.left(), row.top(), std::max(item.width(), row.width() - item.left()), height);
        option.state = QStyle::State_Enabled | (hasFocus() ? QStyle::State_Active : QStyle::State_None);
        option.index = folder;
        itemDelegateForIndex(folder)->paint(&painter, option, folder);
    }
    // A hairline under the pinned rows, as faint as the indent guides.
    const int bottom = int(stickyRows_.size()) * height;
    QColor line = palette().color(QPalette::Text);
    line.setAlpha(isDarkPalette(palette()) ? 45 : 50);
    painter.fillRect(QRect(0, bottom - 1, viewport()->width(), 1), line);
}

void FileTreeView::scrollContentsBy(int dx, int dy)
{
    QTreeView::scrollContentsBy(dx, dy);
    // The pinned rows are drawn over the contents: scrolling would move them.
    if (dy != 0)
        viewport()->update();
}

void FileTreeView::drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const
{
    QTreeView::drawBranches(painter, rect, index);
    drawIndentGuides(painter, rect, index);
    drawOpenMark(painter, rect, index);
}

void FileTreeView::drawOpenMark(QPainter *painter, const QRect &rect, const QModelIndex &index) const
{
    // Where a folder has its arrow; a main file of nested ones has it too.
    if (!index.data(TreeRoles::isOpen).toBool() || model()->hasChildren(index))
        return;
    constexpr qreal radius = 2.5;
    const QPointF center(rect.right() + 1 - indentation() / 2.0, rect.top() + rect.height() / 2.0);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(palette().color(QPalette::Link));
    painter->drawEllipse(center, radius, radius);
    painter->restore();
}

void FileTreeView::drawIndentGuides(QPainter *painter, const QRect &rect, const QModelIndex &index) const
{
    // Indent guides: a fine line under each folder that holds this row,
    // brighter for the folder of the current item.
    const int indent = indentation();
    if (indent <= 0)
        return;
    QList<QModelIndex> folders;
    for (QModelIndex folder = index.parent(); folder.isValid() && folder != rootIndex(); folder = folder.parent())
        folders.prepend(folder);
    if (folders.isEmpty())
        return;
    const QModelIndex active = currentFolder();
    QColor line = palette().color(QPalette::Text);
    line.setAlpha(isDarkPalette(palette()) ? 38 : 45);
    QColor strong = line;
    strong.setAlpha(line.alpha() * 3);
    painter->save();
    // The branches' rect ends where the row's item starts: one indentation
    // per level, the deepest at its right.
    const int itemLeft = rect.right() + 1;
    const int depth = int(folders.size());
    for (int level = 0; level < depth; ++level) {
        const int x = itemLeft - (depth - level + 1) * indent + indent / 2;
        painter->setPen(QPen(folders.at(level) == active ? strong : line, 1));
        painter->drawLine(QPointF(x + 0.5, rect.top()), QPointF(x + 0.5, rect.bottom() + 1));
    }
    painter->restore();
}

QModelIndex FileTreeView::currentFolder() const
{
    const QModelIndex current = currentIndex();
    if (!current.isValid())
        return {};
    return isExpanded(current) ? current : current.parent();
}

bool FileTreeView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip)
        requestLastCommit(static_cast<QHelpEvent *>(event)->pos());
    return QTreeView::viewportEvent(event);
}

void FileTreeView::requestLastCommit(const QPoint &position)
{
    const QModelIndex index = indexAt(position);
    if (!inRepository_ || !index.isValid())
        return;
    if (const QString path = pathOf(index); !proxy_->hasLastCommit(path))
        lastCommits_->request(path);
}

// The tooltip shown meanwhile gets the commit, if still on that file.
void FileTreeView::showLastCommit(const QString &path, const QString &description)
{
    proxy_->setLastCommit(path, description);
    if (description.isEmpty() || !QToolTip::isVisible())
        return;
    const QModelIndex index = indexAt(viewport()->mapFromGlobal(QCursor::pos()));
    if (!index.isValid() || pathOf(index) != path)
        return;
    QToolTip::showText(QCursor::pos(), index.data(Qt::ToolTipRole).toString(), viewport(), visualRect(index));
}

QStringList FileTreeView::localPaths(const QMimeData *data)
{
    QStringList paths;
    if (!data)
        return paths;
    for (const QUrl &url : data->urls()) {
        if (url.isLocalFile())
            paths.append(QDir::cleanPath(url.toLocalFile()));
    }
    return paths;
}

void FileTreeView::putOnClipboard(const QStringList &paths, bool cut)
{
    QList<QUrl> urls;
    QStringList nativePaths;
    for (const QString &path : paths) {
        urls.append(QUrl::fromLocalFile(path));
        nativePaths.append(QDir::toNativeSeparators(path));
    }
    auto *clipboardData = new QMimeData;
    clipboardData->setUrls(urls);
    clipboardData->setText(nativePaths.join(QLatin1Char('\n')));
    if (cut)
        clipboardData->setData(cutFormat, paths.join(QLatin1Char('\n')).toUtf8());
    QApplication::clipboard()->setMimeData(clipboardData);
    proxy_->setCut(cut ? paths : QStringList());
    viewport()->update();
}

void FileTreeView::cutSelection()
{
    const QStringList paths = editableSelection();
    if (paths.isEmpty())
        return;
    putOnClipboard(paths, true);
    emit statusMessage(paths.size() == 1
                           ? tr("“%1” cut: paste it into a folder to move it").arg(QFileInfo(paths.first()).fileName())
                           : tr("%n items cut: paste them into a folder to move them", nullptr, int(paths.size())));
}

void FileTreeView::copySelection()
{
    if (const QStringList paths = editableSelection(); !paths.isEmpty())
        putOnClipboard(paths, false);
}

void FileTreeView::paste()
{
    const QMimeData *clipboardData = QApplication::clipboard()->mimeData();
    const QStringList sources = localPaths(clipboardData);
    if (sources.isEmpty() || rootPath_.isEmpty())
        return;
    const QStringList cut = clipboardData->hasFormat(cutFormat)
                                ? QString::fromUtf8(clipboardData->data(cutFormat)).split(QLatin1Char('\n'))
                                : QStringList();
    const bool move = !cut.isEmpty() && sources == cut;
    transfer(sources, targetFolder(currentIndex()), move);
    if (move) {
        // A cut is pasted once.
        proxy_->setCut({});
        QApplication::clipboard()->clear();
        viewport()->update();
    }
}

void FileTreeView::undo()
{
    const std::optional<FileUndoStack::Step> step = undoStack_.last();
    if (!step) {
        emit statusMessage(tr("No file operation to undo"));
        return;
    }
    // The step stays, to be undone once saved.
    const bool movesAnEditedFile = step->kind != FileUndoStack::Step::Kind::Trashed;
    if (movesAnEditedFile && hasUnsavedChanges_ && hasUnsavedChanges_(step->path)) {
        emit statusMessage(tr("“%1” has unsaved changes: save them first").arg(QFileInfo(step->path).fileName()));
        return;
    }
    const QString description = undoStack_.lastDescription();
    if (const QString error = undoStack_.undo(); !error.isEmpty()) {
        emit statusMessage(error);
        return;
    }
    reportUndone(*step);
    emit statusMessage(tr("Done: %1").arg(description));
}

void FileTreeView::reportUndone(const FileUndoStack::Step &step)
{
    switch (step.kind) {
    case FileUndoStack::Step::Kind::Created:
        emit entryDeleted(step.path);
        break;
    case FileUndoStack::Step::Kind::Moved:
        reveal(step.source);
        emit entryRenamed(step.path, step.source);
        break;
    case FileUndoStack::Step::Kind::Trashed:
        reveal(step.path);
        emit entryCreated(step.path);
        break;
    }
}

void FileTreeView::duplicateCurrent()
{
    const QString path = editablePath(currentIndex());
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    const bool isFolder = info.isDir() && !info.isSymLink();
    const QString relative = QDir(QFileInfo(rootPath_).absolutePath()).relativeFilePath(info.absolutePath());
    auto *popup = new NamePopup(
        isFolder ? tr("Copy Folder") : tr("Copy File"), QDir::toNativeSeparators(relative + QLatin1Char('/')),
        [this, path](const QString &name) {
            const EntryTarget target = resolveRename(path, name);
            if (!target.error.isEmpty())
                return target.error;
            if (target.path == path)
                return tr("A file with the name “%1” already exists").arg(name);
            return transferOne(path, target.path, false);
        },
        this);
    popup->setName(FileOperations::copyName(info.absolutePath(), info.fileName()), isFolder);
    popup->popup();
}

void FileTreeView::transfer(const QStringList &sources, const QString &folder, bool move)
{
    if (folder.isEmpty() || sources.isEmpty())
        return;
    if (sources.size() == 1) {
        transferSingle(sources.first(), folder, move);
        return;
    }
    QStringList errors;
    for (const QString &source : sources) {
        const QString destination =
            QDir(folder).filePath(FileOperations::freeName(folder, QFileInfo(source).fileName()));
        if (const QString error = transferOne(source, destination, move); !error.isEmpty())
            errors.append(error);
    }
    if (!errors.isEmpty())
        emit statusMessage(errors.join(QStringLiteral("; ")));
}

void FileTreeView::transferSingle(const QString &source, const QString &folder, bool move)
{
    const QString destination = QDir(folder).filePath(QFileInfo(source).fileName());
    const bool isAlreadyThere = QDir::cleanPath(QFileInfo(source).absolutePath()) == QDir::cleanPath(folder);
    if (move && isAlreadyThere)
        return;
    if (move && isInsideItself(source, folder)) {
        emit statusMessage(tr("A folder cannot be moved into itself"));
        return;
    }
    if (QFileInfo::exists(destination)) {
        askTransferName(source, folder, move);
        return;
    }
    if (const QString error = transferOne(source, destination, move); !error.isEmpty())
        emit statusMessage(error);
}

// The name is taken in `folder`: asks for another.
void FileTreeView::askTransferName(const QString &source, const QString &folder, bool move)
{
    const QFileInfo info(source);
    const QString name = info.fileName();
    const bool isFolder = info.isDir() && !info.isSymLink();
    const QString relative = QDir(QFileInfo(rootPath_).absolutePath()).relativeFilePath(folder);
    auto *popup = new NamePopup(
        move ? tr("Move") : tr("Copy"), QDir::toNativeSeparators(relative + QLatin1Char('/')),
        [this, source, folder, move](const QString &newName) {
            const EntryTarget target = resolveNewEntry(rootPath_, folder, newName, false);
            if (!target.error.isEmpty())
                return target.error;
            return transferOne(source, target.path, move);
        },
        this);
    const QString suggested = QDir::cleanPath(info.absolutePath()) == QDir::cleanPath(folder)
                                  ? FileOperations::copyName(folder, name)
                                  : FileOperations::freeName(folder, name);
    popup->setName(suggested, isFolder);
    popup->showError(tr("“%1” already exists in this folder").arg(name));
    popup->popup();
}

QString FileTreeView::transferOne(const QString &source, const QString &destination, bool move)
{
    if (move) {
        if (hasUnsavedChanges_ && hasUnsavedChanges_(source)) {
            return QFileInfo(source).isDir() ? tr("Files in this folder have unsaved changes: save them first")
                                             : tr("The file has unsaved changes: save it first");
        }
        if (const QString error = FileOperations::move(source, destination); !error.isEmpty())
            return error;
        undoStack_.record({.kind = FileUndoStack::Step::Kind::Moved, .path = destination, .source = source});
        reveal(destination);
        emit entryRenamed(source, destination);
        emit statusMessage(tr("“%1” moved").arg(QFileInfo(destination).fileName()));
        return {};
    }
    if (const QString error = FileOperations::copy(source, destination); !error.isEmpty())
        return error;
    undoStack_.record({.kind = FileUndoStack::Step::Kind::Created, .path = destination, .source = {}});
    reveal(destination);
    emit entryCreated(destination);
    emit statusMessage(tr("“%1” copied").arg(QFileInfo(destination).fileName()));
    return {};
}

QString FileTreeView::dropFolder(const QPoint &position) const
{
    if (rootPath_.isEmpty())
        return {};
    const QModelIndex index = indexAt(position);
    return index.isValid() ? targetFolder(index) : rootPath_;
}

void FileTreeView::dragEnterEvent(QDragEnterEvent *event)
{
    if (localPaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    QTreeView::dragEnterEvent(event); // auto-scroll, auto-expand
    event->acceptProposedAction();
}

void FileTreeView::dragMoveEvent(QDragMoveEvent *event)
{
    QTreeView::dragMoveEvent(event); // drop indicator, auto-scroll
    const QStringList sources = localPaths(event->mimeData());
    const QString folder = dropFolder(event->position().toPoint());
    const bool internal = event->source() == this;
    const bool copy = !internal || (event->modifiers() & Qt::ControlModifier);
    const bool valid = !folder.isEmpty() && !sources.isEmpty() &&
                       std::none_of(sources.begin(), sources.end(), [&](const QString &source) {
                           return isInsideItself(source, folder) ||
                                  (!copy && QDir::cleanPath(QFileInfo(source).absolutePath()) == folder);
                       });
    if (!valid) {
        event->ignore();
        return;
    }
    event->setDropAction(copy ? Qt::CopyAction : Qt::MoveAction);
    event->accept();
}

void FileTreeView::dropEvent(QDropEvent *event)
{
    const QStringList sources = localPaths(event->mimeData());
    const QString folder = dropFolder(event->position().toPoint());
    const bool copy = event->source() != this || (event->modifiers() & Qt::ControlModifier);
    // Reported as a copy: the view must not try to remove the dragged rows.
    event->setDropAction(Qt::CopyAction);
    event->accept();
    if (sources.isEmpty() || folder.isEmpty())
        return;
    if (copy) {
        transfer(sources, folder, false);
        return;
    }
    // A move is confirmed, as in IntelliJ: a drag can slip.
    const QString relative = QDir(QFileInfo(rootPath_).absolutePath()).relativeFilePath(folder);
    const QString what = sources.size() == 1 ? tr("“%1”").arg(QFileInfo(sources.first()).fileName())
                                             : tr("%1 items").arg(sources.size());
    auto *popup = new ConfirmPopup(
        {tr("Move"), QDir::toNativeSeparators(relative + QLatin1Char('/')),
         tr("Move %1 to “%2”?").arg(what, QDir::toNativeSeparators(relative)), tr("Move")},
        [this, sources, folder](ConfirmPopup *) {
            transfer(sources, folder, true);
            return true;
        },
        this);
    popup->popup();
}
