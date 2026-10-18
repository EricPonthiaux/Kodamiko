#pragma once

#include "git/FilePatch.h"
#include "git/RepositoryStatus.h"
#include "ui/Theme.h"

#include <QThreadPool>
#include <QWidget>

#include <optional>

class CommitMessageGenerator;
class DiffView;
class GitCommands;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// "Diff" mode: pending changes, split as in "git status" between staged ones
// (ready to commit) and the rest. Here you stage or unstage files, hunks or
// lines, discard changes and commit. Operations go through GitCommands.
class ChangesView : public QWidget {
    Q_OBJECT

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
    // Has Claude write the commit message from the changes to commit; while
    // it writes, stops it.
    void generateCommitMessage();

    void applyTheme(const Theme &theme);
    QSplitter *splitter() const { return splitter_; }
    // The bar shown while a merge or rebase is in progress.
    QWidget *operationBar() const { return operationBar_; }
    QLabel *operationText() const { return operationText_; }
    QPushButton *continueButton() const { return continueButton_; }

signals:
    // Path relative to the project folder.
    void openFileRequested(const QString &relativePath);
    void statusMessage(const QString &message);
    // The state read by refresh() is shown.
    void refreshed();
    // The operation in progress (merge, rebase…): go on, leave out the
    // commit that stopped it, or give up.
    void continueRequested();
    void skipRequested();
    void abortRequested();

private:
    enum class Section { Staged, Unstaged };
    struct FileRef {
        Section section;
        int index; // in RepositoryStatus::staged or ::unstaged
    };

    const QList<FileDiff> &files(Section section) const;
    QTreeWidgetItem *sectionItem(Section section) const;
    // Files selected in the list; a section title stands for all its files.
    QList<FileRef> selectedFiles() const;
    QStringList repoPaths(const QList<FileRef> &files) const;
    // Path of the open folder within the repository, to stage or unstage "everything".
    QString folderPathspec() const;

    void onStatusRead(const std::optional<RepositoryStatus> &status, const QString &error, int generation);
    void showStatus(std::optional<RepositoryStatus> status, const QString &error);
    void selectNow(const QString &relativePath);
    void rebuildList();
    void showSection(Section section);
    void onCurrentItemChanged();
    void updateActions();
    void showOperation();

    void stageFiles(const QList<FileRef> &files);
    void unstageFiles(const QList<FileRef> &files);
    void discardFiles(const QList<FileRef> &files);

    // Lines selected in the displayed diff, per file: the selection, or the
    // hunk under `line` without a selection. `lines` tells which of the two.
    QHash<int, PatchSelection> diffSelection(int line, bool *lines) const;
    void applyDiffSelection(int line, bool discard);
    void showDiffMenu(const QPoint &position);
    void showListMenu(const QPoint &position);

    void onAmendToggled(bool amend);
    void onGeneratingChanged(bool generating);
    void showGeneratedMessage(const QString &message);
    void commit();
    void runCommit(const QString &message, bool amend, bool stageEverything);

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
    QSplitter *splitter_;
    QToolButton *stageButton_;
    QToolButton *unstageButton_;
    QToolButton *discardButton_;
    QToolButton *refreshButton_;
    QPlainTextEdit *message_;
    QCheckBox *amend_;
    QToolButton *generateButton_;
    QPushButton *commitButton_;
    CommitMessageGenerator *generator_;

    // Displayed diff: files of one section.
    Section shownSection_ = Section::Unstaged;
    QList<FilePatch> shownPatches_;
};
