#include "FileTreeView.h"

#include "EntryPopups.h"
#include "TreeColumnFit.h"
#include "TreeMetrics.h"

#include "CommandPalette.h"
#include "FileOperations.h"
#include "ProjectTreeModel.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace {

constexpr int pathRole = Qt::UserRole + 100;
// Clipboard marker of files cut in the tree: pasting them moves them.
const QString cutFormat = QStringLiteral("application/x-duet-cut");

// Colors of IntelliJ's file states (Darcula and light themes).
QColor statusColor(FileStatus status, bool dark)
{
    switch (status) {
    case FileStatus::Modified:
    case FileStatus::Renamed: return QColor(dark ? "#6897bb" : "#0032a0");
    case FileStatus::Added: return QColor(dark ? "#629755" : "#067d17");
    case FileStatus::Untracked: return QColor(dark ? "#d1675a" : "#993300");
    case FileStatus::Conflicted: return QColor(dark ? "#d5756c" : "#c00000");
    case FileStatus::Ignored: return QColor(dark ? "#848504" : "#808000");
    }
    return {};
}

QString statusName(FileStatus status)
{
    switch (status) {
    case FileStatus::Modified: return FileTreeView::tr("modified");
    case FileStatus::Renamed: return FileTreeView::tr("renamed");
    case FileStatus::Added: return FileTreeView::tr("added");
    case FileStatus::Untracked: return FileTreeView::tr("not tracked by git");
    case FileStatus::Conflicted: return FileTreeView::tr("in conflict");
    case FileStatus::Ignored: return FileTreeView::tr("ignored by git");
    }
    return {};
}

bool isDarkPalette(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128;
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

} // namespace

// Adds the git colors, Claude's dot and the cut files' dimming to the
// project model, and filters the rows of a scope.
class TreeDecorations : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setRootPath(const QString &rootPath) { rootPath_ = rootPath; }

    void setStatuses(const FileStatusMap &statuses)
    {
        statuses_ = statuses;
        folderStatuses_ = FileStatuses::folderStatuses(statuses, rootPath_);
        hasIgnored_ = std::any_of(statuses.cbegin(), statuses.cend(), [](FileStatus s) { return s == FileStatus::Ignored; });
    }
    void setTouched(const QSet<QString> &paths) { touched_ = paths; }
    void setCut(const QString &path) { cut_ = path; }
    QString cut() const { return cut_; }

    // nullopt: every row; otherwise only these files and the rows that
    // lead to them (folders, main files of nested ones).
    void setScope(const std::optional<QSet<QString>> &files, const QSet<QString> &ancestors)
    {
        beginFilterChange();
        scopeFiles_ = files;
        scopeAncestors_ = ancestors;
        endFilterChange(Direction::Rows);
    }

    std::optional<FileStatus> statusOf(const QString &path, bool isDir) const
    {
        if (const auto it = statuses_.constFind(path); it != statuses_.cend())
            return *it;
        if (isDir) {
            if (const auto it = folderStatuses_.constFind(path); it != folderStatuses_.cend())
                return *it;
        }
        if (hasIgnored_) {
            // In an ignored folder.
            for (QString folder = QFileInfo(path).absolutePath(); folder.size() > rootPath_.size();
                 folder = QFileInfo(folder).absolutePath()) {
                if (statuses_.value(folder, FileStatus::Modified) == FileStatus::Ignored)
                    return FileStatus::Ignored;
            }
        }
        return std::nullopt;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (index.column() != 0 || (role != Qt::ForegroundRole && role != Qt::DecorationRole && role != Qt::ToolTipRole
                                    && role != pathRole))
            return QSortFilterProxyModel::data(index, role);
        const auto *model = static_cast<ProjectTreeModel *>(sourceModel());
        const QModelIndex source = mapToSource(index);
        const QString path = model->filePath(source);
        if (role == pathRole)
            return path;
        const std::optional<FileStatus> status = statusOf(path, model->isDir(source));
        switch (role) {
        case Qt::ForegroundRole:
            if (!cut_.isEmpty() && FileOperations::isInside(path, cut_))
                return QGuiApplication::palette().color(QPalette::Disabled, QPalette::Text);
            if (status)
                return statusColor(*status, isDarkPalette(QGuiApplication::palette()));
            break;
        case Qt::DecorationRole:
            if (touched_.contains(path))
                return badged(QSortFilterProxyModel::data(index, role).value<QIcon>());
            break;
        case Qt::ToolTipRole: {
            QStringList lines{QDir::toNativeSeparators(path)};
            if (status) {
                // A folder colored by the files it contains.
                const bool contains = model->isDir(source) && !statuses_.contains(path) && *status != FileStatus::Ignored;
                lines.append(contains ? FileTreeView::tr("contains changes") : statusName(*status));
            }
            if (touched_.contains(path))
                lines.append(FileTreeView::tr("just edited by Claude"));
            return lines.join(QLatin1Char('\n'));
        }
        default:
            break;
        }
        return QSortFilterProxyModel::data(index, role);
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        if (!scopeFiles_)
            return true;
        const auto *model = static_cast<ProjectTreeModel *>(sourceModel());
        const QString path = model->filePath(model->index(sourceRow, 0, sourceParent));
        return path == rootPath_ || scopeFiles_->contains(path) || scopeAncestors_.contains(path);
    }

private:
    // The file's icon with an orange dot (Claude's color) in its corner.
    QIcon badged(const QIcon &icon) const
    {
        const qint64 key = icon.cacheKey();
        if (const auto it = badges_.constFind(key); it != badges_.cend())
            return *it;
        QIcon result;
        for (const qreal ratio : {1.0, 2.0}) {
            QPixmap pixmap = icon.pixmap(QSize(16, 16), ratio);
            if (pixmap.isNull()) {
                pixmap = QPixmap(QSize(16, 16) * ratio);
                pixmap.setDevicePixelRatio(ratio);
                pixmap.fill(Qt::transparent);
            }
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(QGuiApplication::palette().color(QPalette::Base), 1.2));
            painter.setBrush(QColor("#d97757"));
            painter.drawEllipse(QRectF(8.5, 8.5, 7, 7));
            result.addPixmap(pixmap);
        }
        badges_.insert(key, result);
        return result;
    }

    QString rootPath_;
    QString cut_;
    FileStatusMap statuses_;
    FileStatusMap folderStatuses_;
    bool hasIgnored_ = false;
    QSet<QString> touched_;
    std::optional<QSet<QString>> scopeFiles_;
    QSet<QString> scopeAncestors_;
    mutable QHash<qint64, QIcon> badges_;
};

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
        if (view_->speedSearch_.isEmpty() || index.column() != 0)
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
    FileTreeView *view_;
};

FileTreeView::FileTreeView(QWidget *parent)
    : QTreeView(parent)
    , model_(new ProjectTreeModel(this))
    , proxy_(new TreeDecorations(this))
{
    proxy_->setSourceModel(model_);
    setModel(proxy_);
    header()->hide();
    setAnimated(false);
    setUniformRowHeights(true);

    new TreeColumnFit(this);
    setItemDelegate(new FileTreeDelegate(this));

    speedSearchLabel_ = new QLabel(viewport());
    speedSearchLabel_->setAutoFillBackground(true);
    speedSearchLabel_->setMargin(3);
    speedSearchLabel_->hide();

    connect(proxy_, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &parent) {
        if (scope_ != Scope::Project)
            expandScope(parent);
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

void FileTreeView::showContextMenu(const QPoint &position)
{
    if (rootPath_.isEmpty())
        return;
    const QModelIndex index = indexAt(position);
    if (index.isValid())
        setCurrentIndex(index);
    const QString folder = targetFolder(index);
    const QString clicked = index.isValid() ? pathOf(index) : rootPath_;

    QMenu menu;
    menu.addAction(tr("New &File…"), this, [this, folder] { createEntry(folder, false); });
    menu.addAction(tr("New F&older…"), this, [this, folder] { createEntry(folder, true); });
    menu.addSeparator();
    const QString path = editablePath(index);
    menu.addAction(tr("Cu&t"), QKeySequence::Cut, this, &FileTreeView::cutCurrent)->setEnabled(!path.isEmpty());
    menu.addAction(tr("&Copy"), QKeySequence::Copy, this, &FileTreeView::copyCurrent)->setEnabled(!path.isEmpty());
    menu.addAction(tr("&Paste"), QKeySequence::Paste, this, &FileTreeView::paste)
        ->setEnabled(!localPaths(QApplication::clipboard()->mimeData()).isEmpty());
    if (!path.isEmpty()) {
        menu.addAction(tr("Cop&y…"), QKeySequence(Qt::Key_F5), this, &FileTreeView::duplicateCurrent);
        menu.addSeparator();
        menu.addAction(tr("&Rename…"), QKeySequence(Qt::Key_F2), this, [this, path] { renameEntry(path); });
        menu.addAction(tr("&Delete…"), QKeySequence::Delete, this, [this, path] { deleteEntry(path); });
    }

    menu.addSeparator();
    QMenu *copy = menu.addMenu(tr("Copy Path/&Reference"));
    copy->addAction(tr("&Absolute Path"), QKeySequence(tr("Ctrl+Shift+C")), this,
                    [this, clicked] { copyPath(clicked, false); });
    copy->addAction(tr("Path From &Project Root"), QKeySequence(tr("Ctrl+Alt+Shift+C")), this,
                    [this, clicked] { copyPath(clicked, true); });
    copy->addAction(tr("File &Name"), this, [clicked] { QApplication::clipboard()->setText(QFileInfo(clicked).fileName()); });
    menu.addAction(tr("Send to &Claude"), QKeySequence(tr("Ctrl+Alt+K")), this,
                   [this, clicked] { emit sendToClaudeRequested(clicked); });
    QMenu *open = menu.addMenu(tr("Open &In"));
    open->addAction(tr("&Terminal"), this, [this, folder] { emit openTerminalRequested(folder); });
    open->addAction(tr("&File Manager"), this,
                    [folder] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
    if (inRepository_)
        addGitMenu(&menu, clicked);
    menu.exec(viewport()->mapToGlobal(position));
}

void FileTreeView::addGitMenu(QMenu *menu, const QString &path)
{
    const bool isDir = QFileInfo(path).isDir();
    const std::optional<FileStatus> status = proxy_->statusOf(path, isDir);
    const bool changed = status && *status != FileStatus::Ignored;
    // Rollback restores the index state: nothing to restore for a file git does not know.
    const bool trackedChange = changed && *status != FileStatus::Untracked;
    QMenu *git = menu->addMenu(tr("&Git"));
    git->addAction(tr("Show &Diff"), this, [this, path] { emit showDiffRequested(path); })->setEnabled(changed);
    git->addAction(tr("&Add"), this, [this, path] { emit stageRequested(path); })->setEnabled(changed);
    git->addAction(tr("&Rollback…"), this, [this, path] { confirmRollback(path); })->setEnabled(trackedChange);
    git->addSeparator();
    git->addAction(tr("Show &History"), this, [this, path] { emit historyRequested(path); })
        ->setEnabled(!status || *status != FileStatus::Untracked);
}

void FileTreeView::confirmRollback(const QString &path)
{
    const QFileInfo info(path);
    const QString relative = QDir(QFileInfo(rootPath_).absolutePath()).relativeFilePath(info.absolutePath());
    const QString message = (info.isDir() ? tr("Restore the files of “%1” to their state in the git index?")
                                          : tr("Restore “%1” to its state in the git index?"))
                                .arg(path == rootPath_ ? QDir::toNativeSeparators(rootPath_) : info.fileName())
        + QLatin1Char('\n') + tr("A snapshot is taken first: Git ▸ Restore a Snapshot brings the changes back.");
    auto *popup = new ConfirmPopup(tr("Rollback"), QDir::toNativeSeparators(relative + QLatin1Char('/')), message,
                                   tr("Rollback"),
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

void FileTreeView::deleteCurrent()
{
    if (const QString path = editablePath(currentIndex()); !path.isEmpty())
        deleteEntry(path);
}

void FileTreeView::keyPressEvent(QKeyEvent *event)
{
    if (handleSpeedSearchKey(event))
        return;
    const QString current = currentIndex().isValid() ? pathOf(currentIndex()) : rootPath_;
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (event->key() == Qt::Key_C && modifiers == (Qt::ControlModifier | Qt::ShiftModifier)) {
        copyPath(current, false);
        return;
    }
    if (event->key() == Qt::Key_C && modifiers == (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) {
        copyPath(current, true);
        return;
    }
    if (event->key() == Qt::Key_K && modifiers == (Qt::ControlModifier | Qt::AltModifier)) {
        emit sendToClaudeRequested(current);
        return;
    }
    if (event->matches(QKeySequence::Cut)) {
        cutCurrent();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        copyCurrent();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        paste();
        return;
    }
    if (event->key() == Qt::Key_F5 && modifiers == Qt::NoModifier) {
        duplicateCurrent();
        return;
    }
    if (event->matches(QKeySequence::Delete)) {
        deleteCurrent();
        return;
    }
    if (event->key() == Qt::Key_F2 && (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
        renameCurrent();
        return;
    }
    // IntelliJ's shortcut.
    if (event->key() == Qt::Key_F6 && event->modifiers() == Qt::ShiftModifier) {
        renameCurrent();
        return;
    }
    QTreeView::keyPressEvent(event);
}

QString FileTreeView::editablePath(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};
    const QString path = pathOf(index);
    return path == rootPath_ || !path.startsWith(rootPath_ + QLatin1Char('/')) ? QString() : path;
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
    auto *popup = new NamePopup(isFolder ? tr("New Folder") : tr("New File"),
                                    QDir::toNativeSeparators(relative + QLatin1Char('/')),
                                    [this, folder, isFolder](const QString &name) {
                                        return createEntryNamed(folder, name, isFolder);
                                    },
                                    this);
    popup->popup();
}

QString FileTreeView::createEntryNamed(const QString &folder, const QString &name, bool isFolder)
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
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return tr("Cannot create “%1”: %2").arg(QDir::toNativeSeparators(path), file.errorString());
    }

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
    auto *popup = new NamePopup(isFolder ? tr("Rename Folder") : tr("Rename File"),
                                QDir::toNativeSeparators(relative + QLatin1Char('/')),
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
    reveal(target.path);
    emit entryRenamed(path, target.path);
    return {};
}

void FileTreeView::deleteEntry(const QString &path)
{
    const QFileInfo info(path);
    const bool isFolder = info.isDir() && !info.isSymLink();
    QString message;
    if (isFolder) {
        // Counted up to a limit: a dependency folder may hold many files.
        constexpr int countLimit = 1000;
        int files = 0;
        for (QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
             it.hasNext() && files <= countLimit; it.next())
            ++files;
        message = files == 0 ? tr("Delete the empty folder “%1”?").arg(info.fileName())
                  : files > countLimit
                      ? tr("Delete the folder “%1” and the more than %2 files it contains?")
                            .arg(info.fileName())
                            .arg(countLimit)
                      : tr("Delete the folder “%1” and the %n file(s) it contains?", nullptr, files)
                            .arg(info.fileName());
    } else {
        message = tr("Delete “%1”?").arg(info.fileName());
    }
    message += QLatin1Char('\n') + tr("It will be moved to the trash.");
    if (hasUnsavedChanges_ && hasUnsavedChanges_(path))
        message += QLatin1Char(' ') + tr("Unsaved changes in the editor will be lost.");

    const QString relative = QDir(QFileInfo(rootPath_).absolutePath()).relativeFilePath(info.absolutePath());
    auto *popup = new ConfirmPopup(
        isFolder ? tr("Delete Folder") : tr("Delete File"), QDir::toNativeSeparators(relative + QLatin1Char('/')),
        message, tr("Delete"),
        [this, path, isFolder, permanently = false](ConfirmPopup *popup) mutable {
            if (!permanently) {
                if (QFile::moveToTrash(path)) {
                    emit entryDeleted(path);
                    return true;
                }
                permanently = true;
                popup->setMessage(tr("“%1” cannot be moved to the trash. Delete it permanently? "
                                     "This cannot be undone.")
                                      .arg(QFileInfo(path).fileName()),
                                  tr("Delete Permanently"));
                return false;
            }
            const bool removed = isFolder ? QDir(path).removeRecursively() : QFile::remove(path);
            if (!removed) {
                popup->showError(tr("Cannot delete “%1”").arg(QFileInfo(path).fileName()));
                return false;
            }
            emit entryDeleted(path);
            return true;
        },
        this);
    popup->popup();
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

bool FileTreeView::select(const QString &path)
{
    const QString cleanPath = QDir::cleanPath(path);
    if (rootPath_.isEmpty() || !(cleanPath == rootPath_ || cleanPath.startsWith(rootPath_ + QLatin1Char('/'))))
        return false;
    if (scope_ == Scope::ChangedFiles
        && statuses_.value(cleanPath, FileStatus::Ignored) == FileStatus::Ignored)
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
    const auto add = [this, &files, &ancestors](const QString &path) {
        if (!path.startsWith(rootPath_ + QLatin1Char('/')) || !QFileInfo::exists(path))
            return;
        // The model reads the folders leading to it; its rows lead the way.
        const QModelIndex index = model_->index(path);
        if (!index.isValid())
            return;
        files.insert(path);
        for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
            ancestors.insert(model_->filePath(parent));
    };
    if (scope_ == Scope::ChangedFiles) {
        for (auto it = statuses_.cbegin(); it != statuses_.cend(); ++it) {
            if (it.value() != FileStatus::Ignored)
                add(it.key());
        }
    } else {
        for (const QString &path : std::as_const(claudeEdited_))
            add(path);
    }
    proxy_->setScope(files, ancestors);
    expandScope(rootIndex());
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

void FileTreeView::setSpeedSearch(const QString &text)
{
    speedSearch_ = text;
    if (text.isEmpty()) {
        speedSearchLabel_->hide();
        viewport()->update();
        return;
    }
    QModelIndex start = currentIndex().isValid() ? currentIndex() : proxy_->index(0, 0, rootIndex());
    QModelIndex match = findMatch(start, true);
    if (!match.isValid())
        match = findMatch(proxy_->index(0, 0, rootIndex()), true); // wraps around
    const bool dark = isDarkPalette(palette());
    QPalette labelPalette = palette();
    labelPalette.setColor(QPalette::Window, dark ? QColor("#3c3f41") : QColor("#f7f7f7"));
    labelPalette.setColor(QPalette::WindowText,
                          match.isValid() ? palette().color(QPalette::Text) : QColor(dark ? "#e06c75" : "#e45649"));
    speedSearchLabel_->setPalette(labelPalette);
    speedSearchLabel_->setText(tr("Search for: %1").arg(text));
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
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
    if (!speedSearch_.isEmpty()) {
        switch (event->key()) {
        case Qt::Key_Escape:
            setSpeedSearch({});
            return true;
        case Qt::Key_Backspace:
            setSpeedSearch(speedSearch_.chopped(1));
            return true;
        case Qt::Key_Up:
        case Qt::Key_Down: {
            const bool forward = event->key() == Qt::Key_Down;
            const QModelIndex current = currentIndex();
            QModelIndex match = findMatch(forward ? indexBelow(current) : indexAbove(current), forward);
            if (match.isValid()) {
                setCurrentIndex(match);
                scrollTo(match);
            }
            return true;
        }
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
            break;
        }
    }
    const QString text = event->text();
    if (modifiers != Qt::NoModifier || text.isEmpty() || !text.at(0).isPrint())
        return false;
    if (speedSearch_.isEmpty() && text.at(0).isSpace())
        return false;
    setSpeedSearch(speedSearch_ + text);
    return true;
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
    setSpeedSearch({});
    QTreeView::mousePressEvent(event);
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

void FileTreeView::cutCurrent()
{
    const QString path = editablePath(currentIndex());
    if (path.isEmpty())
        return;
    auto *data = new QMimeData;
    data->setUrls({QUrl::fromLocalFile(path)});
    data->setText(QDir::toNativeSeparators(path));
    data->setData(cutFormat, path.toUtf8());
    QApplication::clipboard()->setMimeData(data);
    proxy_->setCut(path);
    viewport()->update();
    emit statusMessage(tr("“%1” cut: paste it into a folder to move it").arg(QFileInfo(path).fileName()));
}

void FileTreeView::copyCurrent()
{
    const QString path = editablePath(currentIndex());
    if (path.isEmpty())
        return;
    auto *data = new QMimeData;
    data->setUrls({QUrl::fromLocalFile(path)});
    data->setText(QDir::toNativeSeparators(path));
    QApplication::clipboard()->setMimeData(data);
    if (!proxy_->cut().isEmpty()) {
        proxy_->setCut({});
        viewport()->update();
    }
}

void FileTreeView::paste()
{
    const QMimeData *data = QApplication::clipboard()->mimeData();
    const QStringList sources = localPaths(data);
    if (sources.isEmpty() || rootPath_.isEmpty())
        return;
    const QString cut = data->hasFormat(cutFormat) ? QString::fromUtf8(data->data(cutFormat)) : QString();
    const bool move = !cut.isEmpty() && sources == QStringList{cut};
    transfer(sources, targetFolder(currentIndex()), move);
    if (move) {
        // A cut is pasted once.
        proxy_->setCut({});
        QApplication::clipboard()->clear();
        viewport()->update();
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
    auto *popup = new NamePopup(isFolder ? tr("Copy Folder") : tr("Copy File"),
                                QDir::toNativeSeparators(relative + QLatin1Char('/')),
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
        const QString source = sources.first();
        const QString name = QFileInfo(source).fileName();
        const QString destination = QDir(folder).filePath(name);
        if (move && QDir::cleanPath(QFileInfo(source).absolutePath()) == QDir::cleanPath(folder))
            return; // already there
        if (move && FileOperations::isInside(folder, source)) {
            emit statusMessage(tr("A folder cannot be moved into itself"));
            return;
        }
        if (!QFileInfo::exists(destination)) {
            if (const QString error = transferOne(source, destination, move); !error.isEmpty())
                emit statusMessage(error);
            return;
        }
        // The name is taken: ask for another.
        const QFileInfo info(source);
        const bool isFolder = info.isDir() && !info.isSymLink();
        const QString relative = QDir(QFileInfo(rootPath_).absolutePath()).relativeFilePath(folder);
        auto *popup = new NamePopup(move ? tr("Move") : tr("Copy"), QDir::toNativeSeparators(relative + QLatin1Char('/')),
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
        return;
    }
    QStringList errors;
    for (const QString &source : sources) {
        const QString destination = QDir(folder).filePath(FileOperations::freeName(folder, QFileInfo(source).fileName()));
        if (const QString error = transferOne(source, destination, move); !error.isEmpty())
            errors.append(error);
    }
    if (!errors.isEmpty())
        emit statusMessage(errors.join(QStringLiteral("; ")));
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
        reveal(destination);
        emit entryRenamed(source, destination);
        emit statusMessage(tr("“%1” moved").arg(QFileInfo(destination).fileName()));
        return {};
    }
    if (const QString error = FileOperations::copy(source, destination); !error.isEmpty())
        return error;
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
    const bool valid = !folder.isEmpty() && !sources.isEmpty()
        && std::none_of(sources.begin(), sources.end(), [&](const QString &source) {
               return FileOperations::isInside(folder, source)
                   || (!copy && QDir::cleanPath(QFileInfo(source).absolutePath()) == folder);
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
    auto *popup = new ConfirmPopup(tr("Move"), QDir::toNativeSeparators(relative + QLatin1Char('/')),
                                   tr("Move %1 to “%2”?").arg(what, QDir::toNativeSeparators(relative)), tr("Move"),
                                   [this, sources, folder](ConfirmPopup *) {
                                       transfer(sources, folder, true);
                                       return true;
                                   },
                                   this);
    popup->popup();
}
