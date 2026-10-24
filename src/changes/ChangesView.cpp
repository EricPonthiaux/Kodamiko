// SPDX-License-Identifier: GPL-3.0-only

#include "ChangesView.h"

#include "ChangeTree.h"
#include "CommitPanel.h"
#include "DiffHeaderBar.h"
#include "DiffView.h"
#include "DiscardConfirmation.h"
#include "HunkActions.h"
#include "git/ChangeKindStyle.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"
#include "git/Integration.h"
#include "ui/AppFonts.h"
#include "ui/FoldedPane.h"
#include "ui/GripSplitter.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>
#include <utility>

namespace {

constexpr int fileColumnWidth = 320;

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
    , fileList_(new QTreeWidget(this))
    , diff_(new DiffView(theme, this))
    , diffHeader_(new DiffHeaderBar(diff_, this))
    , hunkActions_(new HunkActions(diff_))
    , splitter_(new GripSplitter(Qt::Horizontal, this))
    , fileSplitter_(new GripSplitter(Qt::Vertical, this))
    , fileColumn_(new QFrame(this))
    , foldedList_(new FoldedPane(tr("Files"), QStringLiteral("file-list"), tr("Show the file list"), this))
    , foldButton_(new QToolButton(this))
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
    toolButton(discardButton_, tr("Discard…"),
               tr("Discard the unstaged changes to the selected files (a copy is kept)"));
    connect(stageButton_, &QToolButton::clicked, this, [this] { stageFiles(selectedFiles()); });
    connect(unstageButton_, &QToolButton::clicked, this, [this] { unstageFiles(selectedFiles()); });
    connect(discardButton_, &QToolButton::clicked, this, [this] { discardFiles(selectedFiles()); });
    foldButton_->setAutoRaise(true);
    foldButton_->setFocusPolicy(Qt::NoFocus);
    foldButton_->setToolTip(tr("Hide the file list"));
    connect(foldButton_, &QToolButton::clicked, this, [this] { setFileListVisible(false); });
    connect(foldedList_, &FoldedPane::expandRequested, this, [this] { setFileListVisible(true); });

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
    fileActionsLayout->addWidget(foldButton_);

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

    // The files in their folders.
    setUpList();

    connect(commitPanel_, &CommitPanel::statusMessage, this, &ChangesView::statusMessage);
    connect(commitPanel_, &CommitPanel::messageRequested, this, &ChangesView::commitMessageRequested);

    // The column: the file actions, then the list and the commit area,
    // which share its height as the user likes.
    fileSplitter_->setChildrenCollapsible(false);
    fileSplitter_->addWidget(fileList_);
    fileSplitter_->addWidget(commitPanel_);
    fileSplitter_->setStretchFactor(0, 1);
    fileColumn_->setObjectName(QStringLiteral("changesColumn"));
    auto *leftLayout = new QVBoxLayout(fileColumn_);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(fileActions_);
    leftLayout->addWidget(fileSplitter_, 1);

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

    splitter_->addWidget(fileColumn_);
    splitter_->addWidget(right);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({fileColumnWidth, 1080});
    // Dragged down to nothing, the column is folded as by its button.
    connect(splitter_, &QSplitter::splitterMoved, this, [this] {
        if (splitter_->sizes().constFirst() == 0)
            setFileListVisible(false);
    });

    // The folded column's bar, then the columns.
    foldedList_->hide();
    auto *columns = new QHBoxLayout;
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(0);
    columns->addWidget(foldedList_);
    columns->addWidget(splitter_, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(titleBar);
    layout->addWidget(operationArea_);
    layout->addLayout(columns);

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
    showDiff();
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

const FileChange &ChangesView::changeOf(const FileRef &ref) const
{
    return files(ref.section).at(ref.index).change;
}

QList<ChangesView::FileRef> ChangesView::filesInListOrder() const
{
    const QList<FileDiff> &staged = files(Section::Staged);
    const QList<FileDiff> &unstaged = files(Section::Unstaged);
    int s = 0;
    int u = 0;
    // Both sections are in the tree's order (sortAsTheTree()): merged.
    const auto stagedComesFirst = [&] {
        if (s == staged.size())
            return false;
        if (u == unstaged.size())
            return true;
        return !ChangeTree::comesBefore(unstaged[u].change.path, staged[s].change.path);
    };
    QList<FileRef> result;
    while (s < staged.size() || u < unstaged.size()) {
        if (stagedComesFirst())
            result.append({Section::Staged, s++});
        else
            result.append({Section::Unstaged, u++});
    }
    return result;
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

void ChangesView::showStatus(std::optional<RepositoryStatus> status, const QString &error)
{
    error_ = error;

    // Selection to restore: the path of each file.
    const QStringList selected = selectedKeys();
    const QTreeWidgetItem *currentItem = fileList_->currentItem();
    const std::optional<QString> current = currentItem ? std::optional(keyOf(currentItem)) : std::nullopt;
    const DiffPosition position = diffPosition();

    status_ = std::move(status);
    rebuildList();
    restoreSelection(selected, current);
    selectFirstFileIfNone();

    showOperation();
    showDiff();
    summary_->setText(status_ ? statusSummary(*status_) : error);
    if (status_)
        restoreDiffPosition(position);
    updateActions();
}

void ChangesView::selectFile(const QString &relativePath)
{
    pendingSelection_ = relativePath;
    refresh();
}

void ChangesView::showDiff()
{
    const QList<FileRef> selected = selectedFiles();
    shownFiles_.clear();
    std::ranges::copy_if(filesInListOrder(), std::back_inserter(shownFiles_),
                         [&selected](const FileRef &ref) { return selected.contains(ref); });
    shownPatches_.clear();
    QList<DiffFile> shown;
    QSet<int> stagedFiles;
    for (const FileRef &ref : std::as_const(shownFiles_)) {
        const FileDiff &diff = files(ref.section).at(ref.index);
        const bool isStaged = ref.section == Section::Staged;
        const QString status = isStaged ? tr("%1  ·  staged") : tr("%1  ·  unstaged");
        if (isStaged)
            stagedFiles.insert(int(shown.size()));
        shownPatches_.append(FilePatch::parse(diff.patch));
        shown.append({diff.change.path, status.arg(changeStatusText(diff.change)), shownPatches_.last(),
                      diff.isTooLarge, diff.newText});
    }
    hunkActions_->setStagedFiles(stagedFiles);
    if (!shown.isEmpty())
        diff_->setFiles(shown);
    else
        diff_->showMessage(emptyDiffMessage());
}

QString ChangesView::emptyDiffMessage() const
{
    if (!status_)
        return {};
    if (status_->staged.isEmpty() && status_->unstaged.isEmpty())
        return tr("No changes");
    return tr("Select a file to see its changes");
}

ChangesView::DiffPosition ChangesView::diffPosition() const
{
    DiffPosition position{diffCursorLine(), diff_->firstVisibleLine()};
    position.file = shownFileAt(position.cursorLine);
    if (position.file)
        position.fileStart = diff_->fileStart(int(shownFiles_.indexOf(*position.file)));
    return position;
}

void ChangesView::restoreDiffPosition(const DiffPosition &position)
{
    // The same part of the same file, wherever it moved: the cursor near
    // the block just staged, Space goes on with the next.
    int shift = 0;
    if (position.file) {
        const QString &path = changeOf(*position.file).path;
        const auto samePart = [&](const FileRef &ref) {
            return ref.section == position.file->section && changeOf(ref).path == path;
        };
        if (const auto found = std::ranges::find_if(shownFiles_, samePart); found != shownFiles_.end())
            shift = diff_->fileStart(int(found - shownFiles_.begin())) - position.fileStart;
    }
    diff_->setCursorPosition(std::clamp(position.cursorLine + shift, 0, std::max(0, diff_->lines() - 1)), 0);
    diff_->setFirstVisibleLine(std::max(0, position.firstLine + shift));
}

std::optional<ChangesView::FileRef> ChangesView::shownFileAt(int line) const
{
    const int file = diff_->locate(line).file;
    if (file < 0 || file >= shownFiles_.size())
        return std::nullopt;
    return shownFiles_[file];
}

QStringList ChangesView::repoPaths(const QList<FileRef> &refs) const
{
    QStringList paths;
    if (!status_)
        return paths;
    for (const FileRef &ref : refs) {
        const FileChange &change = changeOf(ref);
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
        return changeOf(ref).kind == ChangeKind::Added;
    });
    const QString question = unstaged.size() == 1
                                 ? tr("Discard changes to \"%1\"?").arg(changeOf(unstaged.first()).path)
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

void ChangesView::setAgent(const QString &name, const QString &iconName)
{
    commitPanel_->setAgent(name, iconName);
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
    foldButton_->setIcon(UiIcons::icon(QStringLiteral("file-list"), color));
    foldedList_->applyTheme(theme);
    commitPanel_->applyTheme(theme);
    diffHeader_->applyTheme(theme);
    alignBars(theme);
}

bool ChangesView::isFileListVisible() const
{
    return !fileColumn_->isHidden();
}

void ChangesView::setFileListVisible(bool visible)
{
    fileColumn_->setVisible(visible);
    foldedList_->setVisible(!visible);
    if (!visible)
        return;
    // Folded by a drag, the column has no width left to come back with.
    const QList<int> sizes = splitter_->sizes();
    if (sizes.constFirst() == 0)
        splitter_->setSizes({fileColumnWidth, std::max(0, sizes.constLast() - fileColumnWidth)});
}

void ChangesView::alignBars(const Theme &theme)
{
    // As high as a side panel's header, unless their controls need more.
    const int height = std::max({UiMetrics::scaled(UiMetrics::panelHeaderHeight), fileActions_->sizeHint().height(),
                                 diffHeader_->sizeHint().height()});
    fileActions_->setFixedHeight(height);
    diffHeader_->setFixedHeight(height);
    foldedList_->setButtonRowHeight(height);
    // Their bottom borders, see ThemeStyle: the bars are at the top of their column.
    splitter_->setHandleRule(1, {height - 1, Qt::LeftEdge | Qt::RightEdge, theme.line});
}
