#include "ChangesView.h"

#include "DiffView.h"
#include "FileChangeDelegate.h"
#include "claude/CommitMessage.h"
#include "git/ChangeKindStyle.h"
#include "git/GitCommands.h"
#include "git/GitOperations.h"
#include "ui/AppFonts.h"
#include "ui/GripSplitter.h"
#include "ui/UiIcons.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTextCursor>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr int sectionRole = Qt::UserRole;
constexpr int indexRole = Qt::UserRole + 1; // -1 on a section title

QString discardHint()
{
    return QObject::tr("A copy is kept: Git menu ▸ Restore a Snapshot.");
}

// "Discard" rather than "Cancel", which would be confused with the Cancel button.
bool confirmDiscard(QWidget *parent, const QString &question, const QString &details)
{
    QMessageBox box(QMessageBox::Warning, QObject::tr("Discard Changes"), question, QMessageBox::NoButton,
                    parent);
    box.setInformativeText(details.isEmpty() ? discardHint() : details + QLatin1Char(' ') + discardHint());
    QPushButton *discard = box.addButton(QObject::tr("Discard"), QMessageBox::DestructiveRole);
    box.setDefaultButton(box.addButton(QObject::tr("Keep"), QMessageBox::RejectRole));
    box.exec();
    return box.clickedButton() == discard;
}

} // namespace

ChangesView::ChangesView(const Theme &theme, GitCommands *commands, QWidget *parent)
    : QWidget(parent)
    , commands_(commands)
    , summary_(new QLabel(this))
    , list_(new QTreeWidget(this))
    , diff_(new DiffView(theme, this))
    , splitter_(new GripSplitter(Qt::Horizontal, this))
    , stageButton_(new QToolButton(this))
    , unstageButton_(new QToolButton(this))
    , discardButton_(new QToolButton(this))
    , message_(new QPlainTextEdit(this))
    , amend_(new QCheckBox(tr("Amend last commit"), this))
    , generateButton_(new QToolButton(this))
    , commitButton_(new QPushButton(tr("Commit"), this))
    , generator_(new CommitMessageGenerator(this))
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
               tr("Discard changes to the selected files (a copy is kept)"));
    connect(stageButton_, &QToolButton::clicked, this, [this] { stageFiles(selectedFiles()); });
    connect(unstageButton_, &QToolButton::clicked, this, [this] { unstageFiles(selectedFiles()); });
    connect(discardButton_, &QToolButton::clicked, this, [this] { discardFiles(selectedFiles()); });

    refreshButton_ = new QToolButton(this);
    QToolButton *refreshButton = refreshButton_;
    toolButton(refreshButton, tr("Refresh"), tr("Reread the repository state (F5)"));
    refreshButton->setShortcut(QKeySequence(Qt::Key_F5));
    connect(refreshButton, &QToolButton::clicked, this, &ChangesView::refresh);

    // "Changes" and the summary, the actions at the right.
    auto *title = new QLabel(tr("Changes"), this);
    AppFonts::setInterfaceFont(title, 1.15);
    summary_->setEnabled(false); // dimmed
    auto *header = new QHBoxLayout;
    header->setContentsMargins(16, 10, 12, 10);
    header->setSpacing(10);
    header->addWidget(title);
    header->addWidget(summary_);
    header->addStretch();
    header->addWidget(stageButton_);
    header->addWidget(unstageButton_);
    header->addWidget(discardButton_);
    header->addSpacing(6);
    header->addWidget(refreshButton);

    // List: two sections, "Staged" then "Unstaged".
    list_->setHeaderHidden(true);
    list_->setRootIsDecorated(false);
    list_->setIndentation(0);
    list_->setItemDelegate(new FileChangeDelegate(list_));
    list_->setFrameShape(QFrame::NoFrame);
    list_->setMouseTracking(true); // hover
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setToolTip(tr("Double-click: open the file in the editor\nRight-click: stage, unstage, discard"));
    for (const Section section : {Section::Staged, Section::Unstaged}) {
        auto *item = new QTreeWidgetItem(list_);
        item->setData(0, sectionRole, static_cast<int>(section));
        item->setData(0, indexRole, -1);
        item->setExpanded(true);
    }
    connect(list_, &QTreeWidget::currentItemChanged, this, &ChangesView::onCurrentItemChanged);
    connect(list_, &QTreeWidget::itemClicked, this, &ChangesView::onCurrentItemChanged);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, &ChangesView::updateActions);
    connect(list_, &QTreeWidget::customContextMenuRequested, this, &ChangesView::showListMenu);
    connect(list_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const int index = item->data(0, indexRole).toInt();
        if (index < 0 || !status_)
            return;
        const FileChange &change = files(static_cast<Section>(item->data(0, sectionRole).toInt())).at(index).change;
        if (change.kind != ChangeKind::Deleted)
            emit openFileRequested(change.path);
    });

    // Commit message and button, in a card below the list.
    message_->setPlaceholderText(tr("Commit message (Ctrl+Enter to commit)"));
    message_->setTabChangesFocus(true);
    message_->setFixedHeight(96);
    commitButton_->setDefault(true);
    commitButton_->setToolTip(tr("Commit the staged changes (Ctrl+Enter)"));
    for (const int key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto *shortcut = new QShortcut(QKeySequence(Qt::CTRL | key), message_, nullptr, nullptr, Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, &ChangesView::commit);
    }
    connect(amend_, &QCheckBox::toggled, this, &ChangesView::onAmendToggled);
    connect(commitButton_, &QPushButton::clicked, this, &ChangesView::commit);

    toolButton(generateButton_, {}, {});
    onGeneratingChanged(false);
    connect(generateButton_, &QToolButton::clicked, this, &ChangesView::generateCommitMessage);
    connect(generator_, &CommitMessageGenerator::runningChanged, this, &ChangesView::onGeneratingChanged);
    connect(generator_, &CommitMessageGenerator::finished, this, &ChangesView::showGeneratedMessage);
    connect(generator_, &CommitMessageGenerator::failed, this, [this](const QString &error) {
        emit statusMessage(tr("Claude could not write the commit message: %1").arg(error.section(QLatin1Char('\n'), 0, 0)));
    });

    auto *commitRow = new QHBoxLayout;
    commitRow->addWidget(amend_);
    commitRow->addStretch();
    commitRow->addWidget(generateButton_);
    commitRow->addWidget(commitButton_);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 10, 10, 10);
    cardLayout->setSpacing(8);
    cardLayout->addWidget(message_);
    cardLayout->addLayout(commitRow);

    auto *left = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(8, 0, 8, 10);
    leftLayout->setSpacing(10);
    leftLayout->addWidget(list_, 1);
    leftLayout->addWidget(card);

    diff_->setContextMenuPolicy(Qt::CustomContextMenu);
    diff_->setToolTip({});
    connect(diff_, &QWidget::customContextMenuRequested, this, &ChangesView::showDiffMenu);

    splitter_->addWidget(left);
    splitter_->addWidget(diff_);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({320, 1080});

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
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
    summary_->clear();
    generator_->cancel();
    message_->clear();
    amend_->setChecked(false);
    updateActions();
}

const QList<FileDiff> &ChangesView::files(Section section) const
{
    return section == Section::Staged ? status_->staged : status_->unstaged;
}

QTreeWidgetItem *ChangesView::sectionItem(Section section) const
{
    return list_->topLevelItem(section == Section::Staged ? 0 : 1);
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
    pool_.start([this, folder = folder_, generation = generation_] {
        QString error;
        std::optional<RepositoryStatus> status;
        try {
            status = RepositoryStatus::read(folder);
            if (!status)
                error = tr("This folder is not in a git repository.");
        } catch (const GitError &exception) {
            error = QString::fromStdString(exception.what());
        }
        QMetaObject::invokeMethod(
            this, [this, status, error, generation] { onStatusRead(status, error, generation); },
            Qt::QueuedConnection);
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
    if (pendingSelection_)
        selectNow(*std::exchange(pendingSelection_, std::nullopt));
    emit refreshed();
}

void ChangesView::showStatus(std::optional<RepositoryStatus> status, const QString &error)
{
    error_ = error;

    // Selection to restore: section and path of each file.
    const auto keyOf = [this](const QTreeWidgetItem *item) {
        const int index = item->data(0, indexRole).toInt();
        const Section section = static_cast<Section>(item->data(0, sectionRole).toInt());
        return std::make_pair(static_cast<int>(section), index < 0 || !status_ ? QString() : files(section).at(index).change.path);
    };
    QList<std::pair<int, QString>> selected;
    for (const QTreeWidgetItem *item : list_->selectedItems())
        selected.append(keyOf(item));
    const std::optional<std::pair<int, QString>> current =
        list_->currentItem() ? std::optional(keyOf(list_->currentItem())) : std::nullopt;
    const Section previousSection = shownSection_;
    const int firstLine = diff_->firstVisibleLine();

    status_ = std::move(status);
    rebuildList();

    {
        const QSignalBlocker blocker(list_);
        for (QTreeWidgetItemIterator it(list_); *it; ++it) {
            const auto key = keyOf(*it);
            if (selected.contains(key))
                (*it)->setSelected(true);
            if (current && key == *current)
                list_->setCurrentItem(*it, 0, QItemSelectionModel::NoUpdate);
        }
    }

    if (!status_) {
        summary_->setText(error);
        showSection(Section::Unstaged);
        updateActions();
        return;
    }

    QString summary = status_->staged.isEmpty() && status_->unstaged.isEmpty()
                          ? tr("No changes since the last commit")
                          : tr("%1 staged · %2 unstaged").arg(status_->staged.size()).arg(status_->unstaged.size());
    if (!status_->info.operation.isEmpty())
        summary += tr(" · %1 in progress").arg(status_->info.operation);
    summary_->setText(summary);

    // Displayed section: that of the current file, otherwise the same as
    // before, otherwise the one that has changes.
    Section section = current ? static_cast<Section>(current->first) : previousSection;
    if (files(section).isEmpty() && !files(section == Section::Staged ? Section::Unstaged : Section::Staged).isEmpty())
        section = section == Section::Staged ? Section::Unstaged : Section::Staged;
    showSection(section);
    if (section == previousSection)
        diff_->setFirstVisibleLine(firstLine);
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
            QTreeWidgetItem *item = sectionItem(section)->child(i);
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
        for (int i = 0; i < diffs.size(); ++i) {
            auto *item = new QTreeWidgetItem(root);
            FileChangeDelegate::setChange(item, diffs[i].change);
            item->setData(0, sectionRole, static_cast<int>(section));
            item->setData(0, indexRole, i);
        }
    }
}

void ChangesView::showSection(Section section)
{
    shownSection_ = section;
    shownPatches_.clear();
    QList<DiffFile> shown;
    if (status_) {
        for (const FileDiff &diff : files(section)) {
            shownPatches_.append(FilePatch::parse(diff.patch));
            shown.append({diff.change.path, changeStatusText(diff.change), shownPatches_.last()});
        }
    }
    if (shown.isEmpty())
        diff_->showMessage(status_ ? (section == Section::Staged ? tr("Nothing is staged") : tr("No changes")) : QString());
    else
        diff_->setFiles(shown);
}

void ChangesView::onCurrentItemChanged()
{
    const QTreeWidgetItem *item = list_->currentItem();
    if (!item || !status_)
        return;
    const auto section = static_cast<Section>(item->data(0, sectionRole).toInt());
    // The title of an empty section leaves the diff as it is.
    if (section != shownSection_ && files(section).isEmpty())
        return;
    if (section != shownSection_)
        showSection(section);
    const int index = item->data(0, indexRole).toInt();
    diff_->setFirstVisibleLine(index >= 0 ? diff_->fileStart(index) : 0);
}

QList<ChangesView::FileRef> ChangesView::selectedFiles() const
{
    QList<FileRef> result;
    if (!status_)
        return result;
    const auto add = [&result](Section section, int index) {
        const bool known = std::any_of(result.begin(), result.end(),
                                       [&](const FileRef &ref) { return ref.section == section && ref.index == index; });
        if (!known)
            result.append({section, index});
    };
    for (const QTreeWidgetItem *item : list_->selectedItems()) {
        const auto section = static_cast<Section>(item->data(0, sectionRole).toInt());
        const int index = item->data(0, indexRole).toInt();
        if (index >= 0) {
            add(section, index);
            continue;
        }
        for (int i = 0; i < files(section).size(); ++i)
            add(section, i);
    }
    return result;
}

QStringList ChangesView::repoPaths(const QList<FileRef> &refs) const
{
    QStringList paths;
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
    return status_->info.prefix.isEmpty() ? QStringLiteral(".") : status_->info.prefix;
}

void ChangesView::updateActions()
{
    const bool busy = commands_->isBusy();
    const QList<FileRef> selection = selectedFiles();
    const auto any = [&selection](Section section) {
        return std::any_of(selection.begin(), selection.end(), [section](const FileRef &ref) { return ref.section == section; });
    };
    stageButton_->setEnabled(!busy && any(Section::Unstaged));
    unstageButton_->setEnabled(!busy && any(Section::Staged));
    discardButton_->setEnabled(!busy && any(Section::Unstaged));
    commitButton_->setEnabled(!busy && status_.has_value());
    amend_->setEnabled(status_ && !status_->info.unborn);
}

void ChangesView::stageFiles(const QList<FileRef> &refs)
{
    QList<FileRef> unstaged;
    std::copy_if(refs.begin(), refs.end(), std::back_inserter(unstaged),
                 [](const FileRef &ref) { return ref.section == Section::Unstaged; });
    if (unstaged.isEmpty())
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
    if (staged.isEmpty())
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
    if (unstaged.isEmpty())
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
    if (!confirmDiscard(this, question, details))
        return;
    commands_->run(tr("Discarding changes"), [info = status_->info, paths = repoPaths(unstaged)] {
        GitOperations::discard(info, paths);
        return QObject::tr("Changes discarded. %1").arg(discardHint());
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

QHash<int, PatchSelection> ChangesView::diffSelection(int line, bool *lines) const
{
    // Displayed file that contains a diff line, and the position in its patch.
    const auto locate = [this](int diffLine) -> std::tuple<int, int, int> {
        const DiffView::Location location = diff_->locate(diffLine);
        return {location.file, location.hunk, location.line};
    };

    QHash<int, PatchSelection> result;
    *lines = false;
    if (diff_->hasSelectedText()) {
        int lineFrom = 0;
        int indexFrom = 0;
        int lineTo = 0;
        int indexTo = 0;
        diff_->getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);
        if (indexTo == 0 && lineTo > lineFrom)
            --lineTo; // selection that ends at the start of a line
        for (int l = lineFrom; l <= lineTo; ++l) {
            const auto [file, hunk, hunkLine] = locate(l);
            if (file >= 0 && hunk >= 0 && hunkLine >= 0 && shownPatches_[file].hunks[hunk].isChange(hunkLine))
                result[file][hunk].insert(hunkLine);
        }
        if (!result.isEmpty()) {
            *lines = true;
            return result;
        }
    }
    const auto [file, hunk, hunkLine] = locate(line);
    Q_UNUSED(hunkLine);
    if (file >= 0 && hunk >= 0)
        result[file] = shownPatches_[file].selectHunk(hunk);
    return result;
}

void ChangesView::applyDiffSelection(int line, bool discard)
{
    if (!status_)
        return;
    bool lines = false;
    const QHash<int, PatchSelection> selection = diffSelection(line, &lines);
    if (selection.isEmpty())
        return;

    // Stage: patch applied as is to the index. Unstage: applied in reverse
    // to the index. Discard: applied in reverse to the files.
    const PatchDirection direction =
        shownSection_ == Section::Unstaged && !discard ? PatchDirection::Forward : PatchDirection::Reverse;
    QByteArray patch;
    QList<FileRef> touched;
    for (auto it = selection.begin(); it != selection.end(); ++it) {
        patch += shownPatches_[it.key()].partial(it.value(), direction);
        touched.append({shownSection_, it.key()});
    }
    if (patch.isEmpty())
        return;

    if (discard) {
        if (!confirmDiscard(this, lines ? tr("Discard the selected lines?") : tr("Discard this hunk?"), {}))
            return;
        commands_->run(tr("Discarding changes"), [info = status_->info, paths = repoPaths(touched), patch] {
            GitOperations::discardPatch(info, paths, patch);
            return QObject::tr("Changes discarded. %1").arg(discardHint());
        });
        return;
    }
    commands_->run(shownSection_ == Section::Unstaged ? tr("Staging") : tr("Unstaging"),
                   [info = status_->info, patch, direction] {
                       GitOperations::applyToIndex(info, patch, direction);
                       return QString();
                   });
}

void ChangesView::showDiffMenu(const QPoint &position)
{
    int line = diff_->lineAt(position);
    if (line < 0) {
        int index = 0;
        diff_->getCursorPosition(&line, &index);
    }
    bool lines = false;
    const bool hasSelection = status_ && !diffSelection(line, &lines).isEmpty();
    const bool busy = commands_->isBusy();

    QMenu *menu = diff_->createStandardContextMenu();
    QAction *first = menu->actions().value(0);
    const auto addAction = [&](const QString &text, auto slot) {
        QAction *action = new QAction(text, menu);
        action->setEnabled(hasSelection && !busy);
        connect(action, &QAction::triggered, this, slot);
        menu->insertAction(first, action);
    };
    if (shownSection_ == Section::Unstaged) {
        addAction(lines ? tr("Stage Selected Lines") : tr("Stage This Hunk"),
                  [this, line] { applyDiffSelection(line, false); });
        addAction(lines ? tr("Discard Selected Lines…") : tr("Discard This Hunk…"),
                  [this, line] { applyDiffSelection(line, true); });
    } else {
        addAction(lines ? tr("Unstage Selected Lines") : tr("Unstage This Hunk"),
                  [this, line] { applyDiffSelection(line, false); });
    }
    menu->insertSeparator(first);
    menu->exec(diff_->viewport()->mapToGlobal(position));
    delete menu;
}

void ChangesView::showListMenu(const QPoint &position)
{
    const QList<FileRef> selection = selectedFiles();
    if (selection.isEmpty())
        return;
    const bool busy = commands_->isBusy();
    const auto count = [&selection](Section section) {
        return std::count_if(selection.begin(), selection.end(), [section](const FileRef &ref) { return ref.section == section; });
    };

    QMenu menu;
    if (count(Section::Unstaged) > 0) {
        menu.addAction(tr("Stage"), this, [this, selection] { stageFiles(selection); })->setEnabled(!busy);
        menu.addAction(tr("Discard Changes…"), this, [this, selection] { discardFiles(selection); })
            ->setEnabled(!busy);
    }
    if (count(Section::Staged) > 0)
        menu.addAction(tr("Unstage"), this, [this, selection] { unstageFiles(selection); })->setEnabled(!busy);
    if (selection.size() == 1) {
        const FileChange &change = files(selection.first().section).at(selection.first().index).change;
        if (change.kind != ChangeKind::Deleted) {
            menu.addSeparator();
            menu.addAction(tr("Open File"), this, [this, path = change.path] { emit openFileRequested(path); });
        }
    }
    menu.exec(list_->viewport()->mapToGlobal(position));
}

void ChangesView::focusCommitMessage()
{
    message_->setFocus();
}

void ChangesView::onAmendToggled(bool amend)
{
    if (!status_)
        return;
    const QString lastMessage = status_->info.headMessage.trimmed();
    if (amend && message_->toPlainText().trimmed().isEmpty())
        message_->setPlainText(lastMessage);
    else if (!amend && message_->toPlainText().trimmed() == lastMessage)
        message_->clear();
}

void ChangesView::generateCommitMessage()
{
    if (generator_->isRunning()) {
        generator_->cancel();
        emit statusMessage(tr("Commit message not written"));
        return;
    }
    if (!status_)
        return;
    const bool amend = amend_->isChecked();
    // As commit(): with nothing staged, everything will be.
    const bool stageEverything = status_->staged.isEmpty() && !amend;
    if (stageEverything && status_->unstaged.isEmpty()) {
        emit statusMessage(tr("Nothing to commit"));
        return;
    }
    generator_->start(status_->info, amend, stageEverything, folderPathspec());
    emit statusMessage(tr("Claude is writing the commit message…"));
}

void ChangesView::onGeneratingChanged(bool generating)
{
    generateButton_->setText(generating ? tr("Stop") : tr("Generate"));
    generateButton_->setToolTip(generating ? tr("Stop writing the commit message")
                                           : tr("Have Claude write the commit message from the changes to commit "
                                                "(Ctrl+Alt+G)"));
    message_->setReadOnly(generating);
    message_->setPlaceholderText(generating ? tr("Claude is writing the commit message…")
                                            : tr("Commit message (Ctrl+Enter to commit)"));
}

void ChangesView::showGeneratedMessage(const QString &message)
{
    // Through a cursor, so that Ctrl+Z brings back what was written before.
    QTextCursor cursor(message_->document());
    cursor.select(QTextCursor::Document);
    cursor.insertText(message);
    message_->setTextCursor(cursor);
    message_->setFocus();
    emit statusMessage(tr("Commit message written by Claude: review it, then commit (Ctrl+Enter)"));
}

void ChangesView::commit()
{
    if (!status_ || commands_->isBusy())
        return;
    generator_->cancel();
    const QString message = message_->toPlainText().trimmed();
    const bool amend = amend_->isChecked();
    if (message.isEmpty()) {
        emit statusMessage(tr("Write the commit message first"));
        message_->setFocus();
        return;
    }
    if (status_->staged.isEmpty() && !amend) {
        if (status_->unstaged.isEmpty()) {
            emit statusMessage(tr("Nothing to commit"));
            return;
        }
        const auto answer = QMessageBox::question(
            this, tr("Commit"),
            tr("No changes are staged. Stage all changes (%n file(s)) and commit them?", nullptr,
               static_cast<int>(status_->unstaged.size())));
        if (answer != QMessageBox::Yes)
            return;
        runCommit(message, false, true);
        return;
    }
    runCommit(message, amend, false);
}

void ChangesView::runCommit(const QString &message, bool amend, bool stageEverything)
{
    commands_->run(
        tr("Commit"),
        [info = status_->info, message, amend, stageEverything, path = folderPathspec()] {
            if (stageEverything)
                GitOperations::stage(info, {path});
            const QString id = GitOperations::commit(info, message, amend);
            return amend ? QObject::tr("Last commit replaced by %1").arg(id) : QObject::tr("Created commit %1").arg(id);
        },
        [this] {
            const QSignalBlocker blocker(amend_);
            amend_->setChecked(false);
            message_->clear();
        });
}

void ChangesView::applyTheme(const Theme &theme)
{
    diff_->applyTheme(theme);
    const QColor color = theme.foreground;
    stageButton_->setIcon(UiIcons::icon(QStringLiteral("plus"), color));
    unstageButton_->setIcon(UiIcons::icon(QStringLiteral("minus"), color));
    discardButton_->setIcon(UiIcons::icon(QStringLiteral("discard"), color));
    refreshButton_->setIcon(UiIcons::icon(QStringLiteral("refresh"), color));
    generateButton_->setIcon(UiIcons::icon(QStringLiteral("claude"), color));
}
