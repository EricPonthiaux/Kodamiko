// SPDX-License-Identifier: GPL-3.0-only

#include "ChangesView.h"

#include "ChangeTree.h"
#include "CommitPanel.h"
#include "DiffHeaderBar.h"
#include "DiffView.h"
#include "DiscardConfirmation.h"
#include "FileChangeDelegate.h"
#include "HunkActions.h"
#include "git/ChangeKindStyle.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"
#include "git/Integration.h"
#include "ui/AppFonts.h"
#include "ui/GripSplitter.h"
#include "ui/UiIcons.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr int sectionRole = Qt::UserRole;
constexpr int indexRole = Qt::UserRole + 1; // -1 on a section title or a folder
constexpr int treeIndentation = 14;

// "2 staged · 3 unstaged · rebase in progress", or that nothing changed.
QString statusSummary(const RepositoryStatus &status)
{
    const bool isClean = status.staged.isEmpty() && status.unstaged.isEmpty();
    QString summary =
        isClean ? ChangesView::tr("No changes since the last commit")
                : ChangesView::tr("%1 staged · %2 unstaged").arg(status.staged.size()).arg(status.unstaged.size());
    if (!status.info.operation.isEmpty())
        summary += ChangesView::tr(" · %1 in progress").arg(status.info.operation);
    return summary;
}

// The files in the list's order, folders first: the diff follows it, and
// a file's index is the same in both.
void sortAsTheTree(RepositoryStatus &status)
{
    for (QList<FileDiff> *section : {&status.staged, &status.unstaged}) {
        std::ranges::stable_sort(*section, ChangeTree::comesBefore,
                                 [](const FileDiff &diff) { return diff.change.path; });
    }
}

} // namespace

ChangesView::ChangesView(const Theme &theme, GitCommands *commands, QWidget *parent)
    : QWidget(parent)
    , commands_(commands)
    , summary_(new QLabel(this))
    , list_(new QTreeWidget(this))
    , diff_(new DiffView(theme, this))
    , diffHeader_(new DiffHeaderBar(diff_, this))
    , hunkActions_(new HunkActions(diff_))
    , splitter_(new GripSplitter(Qt::Horizontal, this))
    , stageButton_(new QToolButton(this))
    , unstageButton_(new QToolButton(this))
    , discardButton_(new QToolButton(this))
    , commitPanel_(new CommitPanel(commands, this))
{
    const auto toolButton = [](QToolButton *button, const QString &text, const QString &toolTip) {
        button->setText(text);
        button->setToolTip(toolTip);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFocusPolicy(Qt::NoFocus);
    };
    toolButton(stageButton_, tr("Stage"), tr("Stage the selected files"));
    toolButton(unstageButton_, tr("Unstage"), tr("Unstage the selected files"));
    toolButton(discardButton_, tr("Discard…"), tr("Discard changes to the selected files (a copy is kept)"));
    connect(stageButton_, &QToolButton::clicked, this, [this] { stageFiles(selectedFiles()); });
    connect(unstageButton_, &QToolButton::clicked, this, [this] { unstageFiles(selectedFiles()); });
    connect(discardButton_, &QToolButton::clicked, this, [this] { discardFiles(selectedFiles()); });

    refreshButton_ = new QToolButton(this);
    QToolButton *refreshButton = refreshButton_;
    toolButton(refreshButton, tr("Refresh"), tr("Reread the repository state (F5)"));
    refreshButton->setShortcut(QKeySequence(Qt::Key_F5));
    connect(refreshButton, &QToolButton::clicked, this, &ChangesView::refresh);

    // "Changes" and the summary, the refresh at the right.
    auto *title = new QLabel(tr("Changes"), this);
    AppFonts::setInterfaceFont(title, 1.15);
    summary_->setEnabled(false); // dimmed
    // The title bar, a line under it.
    auto *titleBar = new QFrame(this);
    titleBar->setObjectName(QStringLiteral("viewTitleBar"));
    // As tall as its contents: the panes below take the rest.
    titleBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *header = new QHBoxLayout(titleBar);
    header->setContentsMargins(16, 10, 12, 10);
    header->setSpacing(10);
    header->addWidget(title);
    header->addWidget(summary_);
    header->addStretch();
    header->addWidget(refreshButton);

    // The selected files' actions, right above the list.
    fileActions_ = new QFrame(this);
    fileActions_->setObjectName(QStringLiteral("fileActions"));
    auto *fileActionsLayout = new QHBoxLayout(fileActions_);
    fileActionsLayout->setContentsMargins(4, 4, 4, 4);
    fileActionsLayout->setSpacing(2);
    fileActionsLayout->addWidget(stageButton_);
    fileActionsLayout->addWidget(unstageButton_);
    fileActionsLayout->addStretch();
    fileActionsLayout->addWidget(discardButton_);

    // A merge or rebase stopped on conflicts: what to do, and the way on.
    operationBar_ = new QFrame(this);
    operationBar_->setObjectName(QStringLiteral("card"));
    operationText_ = new QLabel(operationBar_);
    operationText_->setWordWrap(true);
    continueButton_ = new QPushButton(tr("Continue"), operationBar_);
    skipButton_ = new QPushButton(tr("Skip Commit"), operationBar_);
    abortButton_ = new QPushButton(tr("Abort"), operationBar_);
    connect(continueButton_, &QPushButton::clicked, this, &ChangesView::continueRequested);
    connect(skipButton_, &QPushButton::clicked, this, &ChangesView::skipRequested);
    connect(abortButton_, &QPushButton::clicked, this, &ChangesView::abortRequested);
    auto *operationLayout = new QHBoxLayout(operationBar_);
    operationLayout->setContentsMargins(14, 8, 10, 8);
    operationLayout->setSpacing(8);
    operationLayout->addWidget(operationText_, 1);
    operationLayout->addWidget(abortButton_);
    operationLayout->addWidget(skipButton_);
    operationLayout->addWidget(continueButton_);
    // Its margins hide with it: without an operation, the columns start
    // right under the title bar, and the handle's line meets its rule.
    operationArea_ = new QWidget(this);
    auto *operationRow = new QHBoxLayout(operationArea_);
    operationRow->setContentsMargins(8, 0, 8, 8);
    operationRow->addWidget(operationBar_);
    operationArea_->hide();

    // List: two sections, "Unstaged" then "Staged", the one nearer the
    // commit area; their files in their folders.
    list_->setHeaderHidden(true);
    list_->setRootIsDecorated(false);
    list_->setIndentation(treeIndentation);
    list_->setItemDelegate(new FileChangeDelegate(list_));
    list_->setFrameShape(QFrame::NoFrame);
    list_->setMouseTracking(true); // hover
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setToolTip(tr("Double-click: open the file in the editor\nRight-click: stage, unstage, discard"));
    for (const Section section : {Section::Unstaged, Section::Staged}) {
        auto *item = new QTreeWidgetItem(list_);
        item->setData(0, sectionRole, static_cast<int>(section));
        item->setData(0, indexRole, -1);
        item->setExpanded(true);
    }
    connect(list_, &QTreeWidget::currentItemChanged, this, &ChangesView::onCurrentItemChanged);
    connect(list_, &QTreeWidget::itemClicked, this, &ChangesView::onCurrentItemChanged);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, &ChangesView::updateActions);
    connect(list_, &QTreeWidget::customContextMenuRequested, this, &ChangesView::showListMenu);
    // A folder closed stays closed when the list is rebuilt.
    connect(list_, &QTreeWidget::itemCollapsed, this,
            [this](const QTreeWidgetItem *item) { collapsedFolders_.insert(keyOf(item)); });
    connect(list_, &QTreeWidget::itemExpanded, this,
            [this](const QTreeWidgetItem *item) { collapsedFolders_.remove(keyOf(item)); });
    connect(list_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const int index = item->data(0, indexRole).toInt();
        if (index < 0 || !status_)
            return;
        const FileChange &change = files(static_cast<Section>(item->data(0, sectionRole).toInt())).at(index).change;
        if (change.kind != ChangeKind::Deleted)
            emit openFileRequested(change.path, 0);
    });

    connect(commitPanel_, &CommitPanel::statusMessage, this, &ChangesView::statusMessage);
    connect(commitPanel_, &CommitPanel::messageRequested, this, &ChangesView::commitMessageRequested);

    // The column: the file actions, the list, the commit area fixed below.
    auto *left = new QFrame(this);
    left->setObjectName(QStringLiteral("changesColumn"));
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(fileActions_);
    leftLayout->addWidget(list_, 1);
    leftLayout->addWidget(commitPanel_);

    diff_->setContextMenuPolicy(Qt::CustomContextMenu);
    diff_->setToolTip({});
    connect(diff_, &QWidget::customContextMenuRequested, this, &ChangesView::showDiffMenu);
    addDiffShortcuts();
    // A block's buttons act on the whole block, whatever lines are selected.
    connect(hunkActions_, &HunkActions::applyRequested, this, [this](int line) {
        diff_->selectAll(false);
        applyDiffSelection(line, false);
    });
    connect(hunkActions_, &HunkActions::discardRequested, this, [this](int line) {
        diff_->selectAll(false);
        applyDiffSelection(line, true);
    });
    connect(diffHeader_, &DiffHeaderBar::whitespaceChanged, this, [this] {
        updateActions();
        refresh();
    });

    // The diff under its header, which follows the file at the top.
    auto *right = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(diffHeader_);
    rightLayout->addWidget(diff_, 1);

    splitter_->addWidget(left);
    splitter_->addWidget(right);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({320, 1080});

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(titleBar);
    layout->addWidget(operationArea_);
    layout->addWidget(splitter_);

    connect(commands_, &GitCommands::busyChanged, this, [this](bool busy) {
        updateActions();
        if (!busy && isVisible())
            refresh();
    });
    rebuildList();
    updateActions();
    applyTheme(theme);
}

ChangesView::~ChangesView()
{
    pool_.waitForDone();
}

void ChangesView::setFolder(const QString &folder)
{
    folder_ = folder;
    ++generation_;
    pendingSelection_.reset();
    status_.reset();
    error_.clear();
    rebuildList();
    showSection(Section::Unstaged);
    showOperation();
    summary_->clear();
    commitPanel_->reset();
    updateActions();
}

const QList<FileDiff> &ChangesView::files(Section section) const
{
    static const QList<FileDiff> none;
    if (!status_)
        return none;
    return section == Section::Staged ? status_->staged : status_->unstaged;
}

QTreeWidgetItem *ChangesView::sectionItem(Section section) const
{
    return list_->topLevelItem(section == Section::Unstaged ? 0 : 1);
}

void ChangesView::refresh()
{
    if (folder_.isEmpty())
        return;
    if (reading_) {
        refreshPending_ = true;
        return;
    }
    reading_ = true;
    refreshPending_ = false;
    // Every diff of the working tree, untracked files included: long in a
    // large repository, so off the UI thread.
    pool_.start([this, folder = folder_, generation = generation_, whitespace = diffHeader_->whitespace()] {
        QString error;
        std::optional<RepositoryStatus> status;
        try {
            status = RepositoryStatus::read(folder, whitespace);
            if (status)
                sortAsTheTree(*status);
            else
                error = tr("This folder is not in a git repository.");
        } catch (const GitError &exception) {
            error = QString::fromStdString(exception.what());
        }
        QMetaObject::invokeMethod(
            this, [this, status, error, generation] { onStatusRead(status, error, generation); }, Qt::QueuedConnection);
    });
}

void ChangesView::onStatusRead(const std::optional<RepositoryStatus> &status, const QString &error, int generation)
{
    reading_ = false;
    if (generation != generation_ || refreshPending_) {
        refreshPending_ = false;
        refresh(); // outdated: read again
        return;
    }
    // Refreshes are frequent (each git command, each Claude edit) and most
    // change nothing: the list and the diff are rebuilt only on a change.
    if (status != status_ || error != error_)
        showStatus(status, error);
    if (pendingSelection_) {
        const QString path = *pendingSelection_;
        pendingSelection_.reset();
        selectNow(path);
    }
    emit refreshed();
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

void ChangesView::restoreSelection(const QList<ItemKey> &selected, const std::optional<ItemKey> &current)
{
    const QSignalBlocker blocker(list_);
    for (QTreeWidgetItemIterator it(list_); *it; ++it) {
        const ItemKey key = keyOf(*it);
        if (selected.contains(key))
            (*it)->setSelected(true);
        if (current && key == *current)
            list_->setCurrentItem(*it, 0, QItemSelectionModel::NoUpdate);
    }
}

ChangesView::Section ChangesView::sectionToShow(Section preferred) const
{
    const Section other = preferred == Section::Staged ? Section::Unstaged : Section::Staged;
    const bool onlyOtherHasChanges = files(preferred).isEmpty() && !files(other).isEmpty();
    return onlyOtherHasChanges ? other : preferred;
}

void ChangesView::showStatus(std::optional<RepositoryStatus> status, const QString &error)
{
    error_ = error;

    // Selection to restore: section and path of each file.
    QList<ItemKey> selected;
    for (const QTreeWidgetItem *item : list_->selectedItems())
        selected.append(keyOf(item));
    const std::optional<ItemKey> current =
        list_->currentItem() ? std::optional(keyOf(list_->currentItem())) : std::nullopt;
    const Section previousSection = shownSection_;
    const int firstLine = diff_->firstVisibleLine();
    const int cursorLine = diffCursorLine();

    status_ = std::move(status);
    rebuildList();
    restoreSelection(selected, current);

    showOperation();
    if (!status_) {
        summary_->setText(error);
        showSection(Section::Unstaged);
        updateActions();
        return;
    }
    summary_->setText(statusSummary(*status_));

    // Displayed section: that of the current file, otherwise the same as
    // before, otherwise the one that has changes.
    const Section section = sectionToShow(current ? static_cast<Section>(current->first) : previousSection);
    showSection(section);
    if (section == previousSection) {
        // The cursor near the block just staged: Space goes on with the next.
        diff_->setCursorPosition(std::min(cursorLine, diff_->lines() - 1), 0);
        diff_->setFirstVisibleLine(firstLine);
    }
    updateActions();
}

void ChangesView::selectFile(const QString &relativePath)
{
    pendingSelection_ = relativePath;
    refresh();
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
        for (int i = 0; i < diffs.size(); ++i) {
            if (!matches(diffs[i].change.path))
                continue;
            QTreeWidgetItem *item = fileRow(section, i);
            list_->clearSelection();
            list_->setCurrentItem(item);
            list_->scrollToItem(item);
            return;
        }
    }
}

void ChangesView::rebuildList()
{
    const QSignalBlocker blocker(list_);
    list_->clearSelection();
    for (const Section section : {Section::Staged, Section::Unstaged}) {
        QTreeWidgetItem *root = sectionItem(section);
        qDeleteAll(root->takeChildren());
        root->setHidden(!status_);
        if (!status_)
            continue;
        const QList<FileDiff> &diffs = files(section);
        FileChangeDelegate::setSection(root, section == Section::Staged ? tr("Staged") : tr("Unstaged"),
                                       static_cast<int>(diffs.size()));
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

void ChangesView::showSection(Section section)
{
    // The other section has other versions of the files.
    if (section != shownSection_)
        diff_->hideUnchangedLines();
    shownSection_ = section;
    hunkActions_->setStaged(section == Section::Staged);
    shownPatches_.clear();
    QList<DiffFile> shown;
    if (status_) {
        for (const FileDiff &diff : files(section)) {
            shownPatches_.append(FilePatch::parse(diff.patch));
            shown.append(
                {diff.change.path, changeStatusText(diff.change), shownPatches_.last(), diff.isTooLarge, diff.newText});
        }
    }
    if (!shown.isEmpty()) {
        diff_->setFiles(shown);
        return;
    }
    if (!status_)
        diff_->showMessage(QString());
    else
        diff_->showMessage(section == Section::Staged ? tr("Nothing is staged") : tr("No changes"));
}

void ChangesView::onCurrentItemChanged()
{
    QTreeWidgetItem *item = list_->currentItem();
    if (!item || !status_)
        return;
    const auto section = static_cast<Section>(item->data(0, sectionRole).toInt());
    // The title of an empty section leaves the diff as it is.
    if (section != shownSection_ && files(section).isEmpty())
        return;
    if (section != shownSection_)
        showSection(section);
    // A folder or a section: its first file.
    const QList<QTreeWidgetItem *> rows = ChangeTree::filesUnder(item);
    const int index = rows.isEmpty() ? -1 : rows.first()->data(0, indexRole).toInt();
    diff_->setFirstVisibleLine(index >= 0 ? diff_->fileStart(index) : 0);
}

QList<ChangesView::FileRef> ChangesView::selectedFiles() const
{
    QList<FileRef> result;
    if (!status_)
        return result;
    const auto add = [&result](Section section, int index) {
        const bool known = std::any_of(result.begin(), result.end(), [&](const FileRef &ref) {
            return ref.section == section && ref.index == index;
        });
        if (!known)
            result.append({section, index});
    };
    // A folder or a section title stands for the files under it.
    for (QTreeWidgetItem *item : list_->selectedItems()) {
        const auto section = static_cast<Section>(item->data(0, sectionRole).toInt());
        for (const QTreeWidgetItem *row : ChangeTree::filesUnder(item))
            add(section, row->data(0, indexRole).toInt());
    }
    return result;
}

QStringList ChangesView::repoPaths(const QList<FileRef> &refs) const
{
    QStringList paths;
    if (!status_)
        return paths;
    for (const FileRef &ref : refs) {
        const FileChange &change = files(ref.section).at(ref.index).change;
        paths.append(status_->info.repoPath(change.path));
        if (change.kind == ChangeKind::Renamed)
            paths.append(status_->info.repoPath(change.oldPath));
    }
    return paths;
}

QString ChangesView::folderPathspec() const
{
    if (!status_ || status_->info.prefix.isEmpty())
        return QStringLiteral(".");
    return status_->info.prefix;
}

void ChangesView::updateActions()
{
    const bool busy = commands_->isBusy();
    const QList<FileRef> selection = selectedFiles();
    const auto any = [&selection](Section section) {
        return std::any_of(selection.begin(), selection.end(),
                           [section](const FileRef &ref) { return ref.section == section; });
    };
    stageButton_->setEnabled(!busy && any(Section::Unstaged));
    unstageButton_->setEnabled(!busy && any(Section::Staged));
    discardButton_->setEnabled(!busy && any(Section::Unstaged));
    hunkActions_->setEnabled(!busy && canApplyDiffLines());
    commitPanel_->setStatus(status_ ? &*status_ : nullptr, folderPathspec());
}

void ChangesView::showOperation()
{
    const bool hasOperation = status_ && !status_->info.operation.isEmpty();
    operationArea_->setVisible(hasOperation);
    if (!hasOperation)
        return;
    const QString operation = status_->info.operation;
    // A conflicted file shows on both sides of the index.
    QSet<QString> conflicted;
    for (const QList<FileDiff> *section : {&status_->staged, &status_->unstaged}) {
        for (const FileDiff &diff : *section) {
            if (diff.change.kind == ChangeKind::Conflicted)
                conflicted.insert(diff.change.path);
        }
    }
    const int conflicts = static_cast<int>(conflicted.size());
    const QString name = operation.left(1).toUpper() + operation.mid(1);
    if (!Integration::canContinue(operation)) {
        operationText_->setText(tr("%1 in progress: finish it from a terminal.").arg(name));
    } else if (conflicts > 0) {
        operationText_->setText((conflicts == 1 ? tr("%1 in progress · 1 conflict.").arg(name)
                                                : tr("%1 in progress · %2 conflicts.").arg(name).arg(conflicts)) +
                                tr(" Edit the files, stage them once resolved, then continue."));
    } else {
        operationText_->setText(tr("%1 in progress · no conflicts left. Continue to commit the result.").arg(name));
    }
    continueButton_->setVisible(Integration::canContinue(operation));
    continueButton_->setEnabled(conflicts == 0);
    continueButton_->setDefault(conflicts == 0);
    skipButton_->setVisible(Integration::canSkip(operation));
    abortButton_->setVisible(Integration::canContinue(operation));
}

void ChangesView::stageFiles(const QList<FileRef> &refs)
{
    QList<FileRef> unstaged;
    std::copy_if(refs.begin(), refs.end(), std::back_inserter(unstaged),
                 [](const FileRef &ref) { return ref.section == Section::Unstaged; });
    if (unstaged.isEmpty() || !status_)
        return;
    commands_->run(tr("Staging"), [info = status_->info, paths = repoPaths(unstaged)] {
        GitOperations::stage(info, paths);
        return QString();
    });
}

void ChangesView::unstageFiles(const QList<FileRef> &refs)
{
    QList<FileRef> staged;
    std::copy_if(refs.begin(), refs.end(), std::back_inserter(staged),
                 [](const FileRef &ref) { return ref.section == Section::Staged; });
    if (staged.isEmpty() || !status_)
        return;
    commands_->run(tr("Unstaging"), [info = status_->info, paths = repoPaths(staged)] {
        GitOperations::unstage(info, paths);
        return QString();
    });
}

void ChangesView::discardFiles(const QList<FileRef> &refs)
{
    QList<FileRef> unstaged;
    std::copy_if(refs.begin(), refs.end(), std::back_inserter(unstaged),
                 [](const FileRef &ref) { return ref.section == Section::Unstaged; });
    if (unstaged.isEmpty() || !status_)
        return;
    const qsizetype created = std::count_if(unstaged.begin(), unstaged.end(), [this](const FileRef &ref) {
        return files(ref.section).at(ref.index).change.kind == ChangeKind::Added;
    });
    const QString question =
        unstaged.size() == 1
            ? tr("Discard changes to \"%1\"?").arg(files(Section::Unstaged).at(unstaged.first().index).change.path)
            : tr("Discard changes to %n file(s)?", nullptr, static_cast<int>(unstaged.size()));
    const QString details =
        created > 0 ? tr("%n untracked file(s) will be deleted.", nullptr, static_cast<int>(created)) : QString();
    if (!DiscardConfirmation::ask(this, question, details))
        return;
    commands_->run(tr("Discarding changes"), [info = status_->info, paths = repoPaths(unstaged)] {
        GitOperations::discard(info, paths);
        return QObject::tr("Changes discarded. %1").arg(DiscardConfirmation::hint());
    });
}

void ChangesView::stageAll()
{
    if (!status_)
        refresh();
    if (!status_ || status_->unstaged.isEmpty())
        return;
    commands_->run(tr("Staging"), [info = status_->info, path = folderPathspec()] {
        GitOperations::stage(info, {path});
        return QString();
    });
}

void ChangesView::unstageAll()
{
    if (!status_)
        refresh();
    if (!status_ || status_->staged.isEmpty())
        return;
    commands_->run(tr("Unstaging"), [info = status_->info, path = folderPathspec()] {
        GitOperations::unstage(info, {path});
        return QString();
    });
}

void ChangesView::showListMenu(const QPoint &position)
{
    const QList<FileRef> selection = selectedFiles();
    if (selection.isEmpty())
        return;
    const bool busy = commands_->isBusy();
    const auto count = [&selection](Section section) {
        return std::count_if(selection.begin(), selection.end(),
                             [section](const FileRef &ref) { return ref.section == section; });
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
    menu.exec(list_->viewport()->mapToGlobal(position));
}

void ChangesView::focusCommitMessage()
{
    commitPanel_->focusMessage();
}

void ChangesView::generateCommitMessage()
{
    commitPanel_->generateMessage();
}

void ChangesView::showCommitMessage(const QString &message)
{
    commitPanel_->showGeneratedMessage(message);
}

void ChangesView::applyTheme(const Theme &theme)
{
    diff_->applyTheme(theme);
    hunkActions_->applyTheme(theme);
    const QColor color = theme.foreground;
    stageButton_->setIcon(UiIcons::icon(QStringLiteral("plus"), color));
    unstageButton_->setIcon(UiIcons::icon(QStringLiteral("minus"), color));
    discardButton_->setIcon(UiIcons::icon(QStringLiteral("discard"), color));
    refreshButton_->setIcon(UiIcons::icon(QStringLiteral("refresh"), color));
    commitPanel_->applyTheme(theme);
    diffHeader_->applyTheme(theme);
    alignBars(theme);
}

void ChangesView::alignBars(const Theme &theme)
{
    const int height = std::max(fileActions_->sizeHint().height(), diffHeader_->sizeHint().height());
    fileActions_->setFixedHeight(height);
    diffHeader_->setFixedHeight(height);
    // Their bottom borders, see ThemeStyle: the bars are at the top of their column.
    splitter_->setHandleRule(1, {height - 1, Qt::LeftEdge | Qt::RightEdge, theme.line});
}
