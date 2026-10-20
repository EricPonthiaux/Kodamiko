#pragma once

#include "git/FilePatch.h"
#include "git/RepositoryStatus.h"
#include "ui/Theme.h"

#include <QThreadPool>
#include <QWidget>

#include <optional>
#include <utility>

class CommitPanel;
class DiffHeaderBar;
class HunkActions;
class DiffView;
class GitCommands;
class QLabel;
class QPushButton;
class QSplitter;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

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

    QSplitter *splitter() const { return splitter_; }

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
    QTreeWidgetItem *sectionItem(Section section) const;
    // Files selected in the list; a section title stands for all its files.
    QList<FileRef> selectedFiles() const;
    QStringList repoPaths(const QList<FileRef> &refs) const;
    // Path of the open folder within the repository, to stage or unstage "everything".
    QString folderPathspec() const;

    void onStatusRead(const std::optional<RepositoryStatus> &status, const QString &error, int generation);
    // An item of the list as its section and file path, which survive a
    // rebuild of the list; the path is empty on a section title.
    using ItemKey = std::pair<int, QString>;
    ItemKey keyOf(const QTreeWidgetItem *item) const;
    void restoreSelection(const QList<ItemKey> &selected, const std::optional<ItemKey> &current);
    // `preferred`, unless only the other section has changes.
    Section sectionToShow(Section preferred) const;
    void showStatus(std::optional<RepositoryStatus> status, const QString &error);
    void selectNow(const QString &relativePath);
    void rebuildList();
    void showSection(Section section);
    void onCurrentItemChanged();
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
    void showListMenu(const QPoint &position);

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

    QLabel *summary_;
    QWidget *operationBar_;
    QLabel *operationText_;
    QPushButton *continueButton_;
    QPushButton *skipButton_;
    QPushButton *abortButton_;
    QTreeWidget *list_;
    DiffView *diff_;
    DiffHeaderBar *diffHeader_;
    HunkActions *hunkActions_;
    QSplitter *splitter_;
    QToolButton *stageButton_;
    QToolButton *unstageButton_;
    QToolButton *discardButton_;
    QToolButton *refreshButton_;
    CommitPanel *commitPanel_;

    // Displayed diff: files of one section.
    Section shownSection_ = Section::Unstaged;
    QList<FilePatch> shownPatches_;
};
