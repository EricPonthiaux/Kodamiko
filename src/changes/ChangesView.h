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

class CommitPanel;
class DiffHeaderBar;
class HunkActions;
class DiffView;
class FoldedPane;
class GitCommands;
class QFrame;
class QLabel;
class QPushButton;
class QSplitter;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// "Diff" mode: pending changes, split as in "git status" between staged ones
// (ready to commit) and the rest, in one list of files and one diff. A
// file's box says whether its changes are staged (checked), partly staged,
// or not; the diff shows the selected files (a folder: the files under it),
// each with its staged changes, then its unstaged ones, under their own
// title bars. Here you stage or unstage files, hunks or lines,
// discard changes and commit. Operations go through GitCommands. In the
// list, Space stages the selected files, or unstages them when they are all
// staged. In the diff, Space stages (or unstages) the selected lines or the
// block at the cursor and Delete discards them, unless the diff hides
// whitespace changes; F4 opens the file at the cursor's line.
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

    // The file column's splitter: the list, the commit area.
    QSplitter *fileSplitter() const { return fileSplitter_; }

    // The file column (actions, list, commit area); folded away, a thin bar
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

        bool operator==(const FileRef &) const = default;
    };

    // The diff's cursor and scroll position, kept across a rebuild relative
    // to the start of the cursor's file (its staged or unstaged part).
    struct DiffPosition {
        int cursorLine = 0;
        int firstLine = 0;
        std::optional<FileRef> file; // nullopt outside the files
        int fileStart = 0;
    };

    const QList<FileDiff> &files(Section section) const;
    const FileChange &changeOf(const FileRef &ref) const;
    // The files of both sections in the list's order, a file's staged
    // changes before its unstaged ones: the diff's files.
    QList<FileRef> filesInListOrder() const;
    void setUpList();
    // The list's own row, out of view: its files show at the top level.
    QTreeWidgetItem *rootItem() const;
    // The changes of a file's row: staged, unstaged, or both; none on a folder.
    static QList<FileRef> refsOf(const QTreeWidgetItem *row);
    // Files selected in the list; a folder stands for its files.
    QList<FileRef> selectedFiles() const;
    QStringList repoPaths(const QList<FileRef> &refs) const;
    // Path of the open folder within the repository, to stage or unstage "everything".
    QString folderPathspec() const;

    void onStatusRead(const std::optional<RepositoryStatus> &status, const QString &error, int generation);
    // An item of the list as its path, which survives a rebuild of the
    // list: a folder's ends with "/", the list's own row's is empty.
    static QString keyOf(const QTreeWidgetItem *item);
    QStringList selectedKeys() const;
    void restoreSelection(const QStringList &selected, const std::optional<QString> &current);
    // Selects the first file when nothing is selected, so that the diff
    // shows one.
    void selectFirstFileIfNone();
    void showStatus(std::optional<RepositoryStatus> status, const QString &error);
    void selectNow(const QString &relativePath);
    void rebuildList();
    // Closes the folder rows under `parent` that the user had closed, recursively.
    void markFolders(QTreeWidgetItem *parent);
    // Shows the diff of the selected files.
    void showDiff();
    QString emptyDiffMessage() const;
    DiffPosition diffPosition() const;
    void restoreDiffPosition(const DiffPosition &position);
    // The diff's first file for `row` (a folder: for its first file); -1 if none.
    int shownFileOf(QTreeWidgetItem *row) const;
    // The part of a file the diff shows at displayed line `line`; nullopt
    // outside the files.
    std::optional<FileRef> shownFileAt(int line) const;
    void onCurrentItemChanged();
    void onSelectionChanged();
    // A file's box was clicked: stages it, or unstages it when unchecked.
    void onItemChanged(QTreeWidgetItem *item);
    // Stages the selected files, or unstages them when they are all staged.
    void toggleSelectedFiles();
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

    // The lines of `selection` in the diff's parts of `section`, as a patch
    // to apply in `direction`, and the parts they are in.
    struct PartialPatch {
        QByteArray patch;
        QList<FileRef> files;
    };

    PartialPatch partialPatch(const QHash<int, PatchSelection> &selection,
                              Section section,
                              PatchDirection direction) const;
    // Lines and blocks can be applied: their patch matches the files, which
    // a diff that ignores whitespace does not.
    bool canApplyDiffLines() const;
    void applyDiffSelection(int line, bool discard);
    void addDiffShortcuts();
    int diffCursorLine() const;
    // The file of displayed line `line` in the editor, at the line facing it.
    void openFileAt(int line);
    void showDiffMenu(const QPoint &position);
    void showListMenu(const QPoint &position);
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
    QSet<QString> collapsedFolders_;          // their keys, see keyOf()

    QLabel *summary_;
    QWidget *operationArea_; // the bar and its margins, hidden together
    QWidget *operationBar_;
    QLabel *operationText_;
    QPushButton *continueButton_;
    QPushButton *skipButton_;
    QPushButton *abortButton_;
    QTreeWidget *fileList_;
    DiffView *diff_;
    DiffHeaderBar *diffHeader_;
    HunkActions *hunkActions_;
    GripSplitter *splitter_;
    GripSplitter *fileSplitter_; // in the file column: the lists and the commit area
    QFrame *fileColumn_;         // the splitter's first pane
    FoldedPane *foldedList_;
    QToolButton *foldButton_;
    QFrame *fileActions_; // Stage, Unstage, Discard, above the list
    QToolButton *stageButton_;
    QToolButton *unstageButton_;
    QToolButton *discardButton_;
    QToolButton *refreshButton_;
    CommitPanel *commitPanel_;

    // Displayed diff: the files' parts, staged or unstaged, and their patches.
    QList<FileRef> shownFiles_;
    QList<FilePatch> shownPatches_;
};
