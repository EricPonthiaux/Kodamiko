// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/FilePatch.h"
#include "git/RepositoryStatus.h"
#include "ui/GripSplitter.h"
#include "ui/Theme.h"

#include <QSet>
#include <QThreadPool>
#include <QWidget>

#include <optional>
#include <utility>

class CommitPanel;
class DiffHeaderBar;
class HunkActions;
class DiffView;
class FoldedFileList;
class GitCommands;
class QFrame;
class QLabel;
class QPushButton;
class QSplitter;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class ViewHeader;

// "Diff" mode: pending changes, split as in "git status" between staged ones
// (ready to commit) and the rest. Here you stage or unstage files, hunks or
// lines, discard changes and commit. Operations go through GitCommands. In
// the diff, Space stages (or unstages) the selected lines or the block at
// the cursor and Delete discards them, unless the diff hides whitespace
// changes; F4 opens the file at the cursor's line.
class ChangesView : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ChangesView)

public:
    ChangesView(const Theme &theme, GitCommands *commands, QWidget *parent = nullptr);
    ~ChangesView() override;

    void setFolder(const QString &folder);
    // Rereads the repository state off the UI thread, then shows it
    // (refreshed()), keeping the selection and the diff position.
    void refresh();

    // Rereads the changes and shows those of `relativePath` (a file, or
    // the first changed file of a folder; empty: the whole folder).
    void selectFile(const QString &relativePath);

    void stageAll();
    void unstageAll();
    void focusCommitMessage();
    // Asks the Claude session for the commit message of the changes to
    // commit (commitMessageRequested()); see showCommitMessage().
    void generateCommitMessage();
    // Puts `message`, written by Claude, in the commit field.
    void showCommitMessage(const QString &message);

    void applyTheme(const Theme &theme);
    // The agent that writes commit messages (see CommitPanel::setAgent()).
    void setAgent(const QString &name, const QString &iconName);

    QSplitter *splitter() const { return splitter_; }

    // The file column's splitter: the unstaged list, the staged one, the commit area.
    QSplitter *fileSplitter() const { return fileSplitter_; }

    // The file column (actions, lists, commit area); folded away, a thin bar
    // stands for it at the left edge. Visible at first.
    bool isFileListVisible() const;
    void setFileListVisible(bool visible);

    // The bar shown while a merge or rebase is in progress.
    QWidget *operationBar() const { return operationBar_; }

    QLabel *operationText() const { return operationText_; }

    QPushButton *continueButton() const { return continueButton_; }

signals:
    // Path relative to the project folder; `line` from 0.
    void openFileRequested(const QString &relativePath, int line);
    void statusMessage(const QString &message);
    // `request`, to type in the Claude session's prompt.
    void commitMessageRequested(const QString &request);
    // The state read by refresh() is shown.
    void refreshed();
    // The operation in progress (merge, rebase…): go on, leave out the
    // commit that stopped it, or give up.
    void continueRequested();
    void skipRequested();
    void abortRequested();

private:
    enum class Section {
        Staged,
        Unstaged
    };

    struct FileRef {
        Section section;
        int index; // in RepositoryStatus::staged or ::unstaged
    };

    const QList<FileDiff> &files(Section section) const;
    QTreeWidget *list(Section section) const;
    ViewHeader *listHeader(Section section) const;
    // The list of `section` under its bar.
    QWidget *listPane(Section section);
    void setUpList(Section section);
    // The row standing for the section's files, the root of its list, out of view.
    QTreeWidgetItem *sectionItem(Section section) const;
    // Files selected in the lists; a folder stands for its files.
    QList<FileRef> selectedFiles() const;
    QStringList repoPaths(const QList<FileRef> &refs) const;
    // Path of the open folder within the repository, to stage or unstage "everything".
    QString folderPathspec() const;

    void onStatusRead(const std::optional<RepositoryStatus> &status, const QString &error, int generation);
    // An item of a list as its section and file path, which survive a
    // rebuild of the list; the path is empty on a section's row.
    using ItemKey = std::pair<int, QString>;
    ItemKey keyOf(const QTreeWidgetItem *item) const;
    QList<ItemKey> selectedKeys() const;
    void restoreSelection(const QList<ItemKey> &selected, const std::optional<ItemKey> &current);
    // `preferred`, unless only the other section has changes.
    Section sectionToShow(Section preferred) const;
    void showStatus(std::optional<RepositoryStatus> status, const QString &error);
    void selectNow(const QString &relativePath);
    void rebuildList();
    // Gives the folder rows under `parent` their section, recursively, and
    // closes those the user had closed.
    void markFolders(QTreeWidgetItem *parent, Section section);
    // The row of file `index` of `section`; nullptr if there is none.
    QTreeWidgetItem *fileRow(Section section, int index) const;
    void showSection(Section section);
    void onCurrentItemChanged(const QTreeWidget *tree);
    // Clears the other list's selection.
    void onSelectionChanged(Section section);
    // Opens the file of `item` in the editor (signal openFileRequested()).
    void openItem(const QTreeWidgetItem *item);
    void updateActions();
    void showOperation();

    void stageFiles(const QList<FileRef> &refs);
    void unstageFiles(const QList<FileRef> &refs);
    void discardFiles(const QList<FileRef> &refs);

    // Lines selected in the displayed diff, per file: the selection, or the
    // hunk under `line` without a selection. `lines` tells which of the two.
    QHash<int, PatchSelection> diffSelection(int line, bool *lines) const;
    // The changed lines among the selected ones, per file; empty without a selection.
    QHash<int, PatchSelection> selectedChanges() const;
    // Lines and blocks can be applied: their patch matches the files, which
    // a diff that ignores whitespace does not.
    bool canApplyDiffLines() const;
    void applyDiffSelection(int line, bool discard);
    void addDiffShortcuts();
    int diffCursorLine() const;
    // The file of displayed line `line` in the editor, at the line facing it.
    void openFileAt(int line);
    void showDiffMenu(const QPoint &position);
    void showListMenu(const QTreeWidget *tree, const QPoint &position);
    // The file actions and the diff's header, side by side, take the same
    // height, and their rules make one line across the handle between them.
    void alignBars(const Theme &theme);

    GitCommands *commands_;
    QString folder_;
    std::optional<RepositoryStatus> status_;
    QString error_; // why there is no status_

    // Reading runs on pool_, one read at a time; a refresh() during a read
    // reads again after it. generation_ changes with the folder.
    QThreadPool pool_;
    int generation_ = 0;
    bool reading_ = false;
    bool refreshPending_ = false;
    std::optional<QString> pendingSelection_; // for selectFile()
    QSet<ItemKey> collapsedFolders_;

    QLabel *summary_;
    QWidget *operationArea_; // the bar and its margins, hidden together
    QWidget *operationBar_;
    QLabel *operationText_;
    QPushButton *continueButton_;
    QPushButton *skipButton_;
    QPushButton *abortButton_;
    ViewHeader *unstagedHeader_;
    QTreeWidget *unstagedList_;
    ViewHeader *stagedHeader_;
    QTreeWidget *stagedList_;
    DiffView *diff_;
    DiffHeaderBar *diffHeader_;
    HunkActions *hunkActions_;
    GripSplitter *splitter_;
    GripSplitter *fileSplitter_; // in the file column: the lists and the commit area
    QFrame *fileColumn_;         // the splitter's first pane
    FoldedFileList *foldedList_;
    QToolButton *foldButton_;
    QFrame *fileActions_; // Stage, Unstage, Discard, above the list
    QToolButton *stageButton_;
    QToolButton *unstageButton_;
    QToolButton *discardButton_;
    QToolButton *refreshButton_;
    CommitPanel *commitPanel_;

    // Displayed diff: files of one section.
    Section shownSection_ = Section::Unstaged;
    QList<FilePatch> shownPatches_;
};
