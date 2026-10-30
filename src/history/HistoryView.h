// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/CommitLog.h"
#include "git/FileBlame.h"
#include "ui/Theme.h"

#include <QColor>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <memory>
#include <optional>

class ChangeMap;
class CommitDelegate;
class DiffFileStrip;
class DiffHeaderBar;
class DiffView;
class FileLens;
class FoldedPane;
class GripSplitter;
class HistorySearchField;
class QLabel;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class ViewHeader;

// "History" mode: commits of HEAD (those touching the open folder),
// searchable (HistorySearchField): the commits read are filtered as the
// query is typed; a search of their changes reads the history again. Two
// panes: the commits, and the chosen commit's changes, whole: the strip of
// its files (DiffFileStrip) and the diff of all of them against the first
// parent, with nothing more to choose. A click on a commit also unfolds its
// row to show its message, folding the one unfolded before; a second click
// folds it. The commits fold away into a thin bar at the left edge; folded,
// the title bar names the chosen commit. A file's
// history can show its lines by age instead of the diff (Lines by Age,
// FileLens), the chosen commit's lines lit. The repository is only read.
class HistoryView : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(HistoryView)

public:
    explicit HistoryView(const Theme &theme, QWidget *parent = nullptr);
    ~HistoryView() override;

    void setFolder(const QString &folder);
    // Reloads the history off the UI thread, then shows it (refreshed());
    // keeps the chosen commit if it still exists.
    void refresh();
    void focusFilter();
    // Reloads without filters and selects this commit (a full or abbreviated ID).
    void revealCommit(const QString &id);
    // Only the commits that modify `relativePath` (a file or folder of the
    // open folder); empty shows them all again. Reloads the history.
    void setPathFilter(const QString &relativePath);
    void applyTheme(const Theme &theme);

    // The commits folded away; not at first.
    void setCommitListFolded(bool folded);

    bool isCommitListFolded() const { return isCommitListFolded_; }

    // Pane proportions and the fold, for the settings.
    QByteArray saveState() const;
    void restoreState(const QByteArray &state);

signals:
    // Path relative to the project folder.
    void openFileRequested(const QString &relativePath);
    // The history read by refresh() is shown.
    void refreshed();
    // The commit's context menu: check it out without a branch.
    void checkoutRequested(const QString &id);

protected:
    void resizeEvent(QResizeEvent *event) override;
    // The list resized: its unfolded message wraps again.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createCommitListPane();
    QWidget *createDiffPane();
    // The commits' header and the diff's take the same height, and the
    // handle between the panes carries their rule.
    void alignBars();
    // Shows the commits unfolded, or the bar of the folded commits.
    void showPanes();
    // The chosen commit's summary in the title bar, while the commits are folded.
    void showCurrentCommitTitle();
    // The log being read is outdated: its walk stops, its result is dropped.
    void abandonReading();
    // The history read again from its newest commits.
    void restartReading();
    void onLogRead(const std::optional<CommitLog> &log, const QString &error, int generation);
    void showLog(std::optional<CommitLog> log, const QString &error);

    // The details of commit `id`, as read with `whitespace`.
    struct DetailsRequest {
        QString id;
        Whitespace whitespace;
    };

    void onDetailsRead(const DetailsRequest &request,
                       const std::optional<CommitDetails> &details,
                       const QString &error);
    // Reads twice as many commits when the end of the list is in view: the
    // list scrolled to its end, or rows hidden by the filter.
    void loadMoreIfAtEnd();
    // Whether the last commit read is older than the query's after: date:
    // the commits read next would all be hidden.
    bool isPastFirstDay() const;
    // The last row the filter leaves is in the viewport.
    bool isEndInView() const;
    // The chosen commit again, as the header's whitespace button says; the
    // diff stays on the file it showed.
    void rereadCommit();
    // The file or folder whose changes commit `commitId` shows: the path
    // filter, or the name the filtered file had before a rename.
    QString filterOf(const QString &commitId) const;
    // Scrolls the diff to the commit's file at `path`, if shown.
    void chooseFile(const QString &path);
    // The diff of the commit's files the path filter keeps; a placeholder
    // without a commit.
    void showCommitFiles();
    // A file of the strip, double-clicked: opened if it still exists.
    void openFile(const QString &relativePath);
    void rebuildList();
    void onQueryChanged();
    // The query's content search, typed a moment ago: the history read again.
    void searchContents();
    void applyFilter();
    // The first commit shown of each day carries the day's heading.
    void headDays();
    void updateSummary();
    void onCommitSelectionChanged();
    // Pressed with the left button: `item` unfolds, the others fold;
    // unfolded already, it folds.
    void toggleUnfolded(QTreeWidgetItem *item);
    // Only `item` unfolded; nullptr: none.
    void unfoldOnly(QTreeWidgetItem *item);
    void showCommit(const QString &id);
    void forgetCommit();
    QString diffPlaceholder() const;
    // The filter, named in the title, without reloading.
    void setPathFilterShown(const QString &relativePath);
    void showDetails();
    void showCommitMenu(const QPoint &position);
    // The file's lines by age in place of the diff, read again; or the diff.
    void showLinesByAge(bool shown);
    // Who wrote each line of the filtered file, read off the UI thread.
    void readBlame();
    void onBlameRead(const QString &path, const std::optional<FileBlame> &blame, const QString &error, int generation);
    // Commit `id`, clicked in the lens, chosen in the list if it was read.
    void chooseCommitOfLens(const QString &id);
    // The panes' sizes from the diff's share; folded, the commits leave it theirs.
    void applyShares();
    void rememberShares();

    QString folder_;
    QString pathFilter_;
    bool filtersOnFile_ = false; // a file's history: Lines by Age offered
    QLabel *title_;              // names the filtered file
    QToolButton *showAll_;
    QToolButton *linesByAge_;  // shown with a file's history
    QToolButton *allBranches_; // checked: every branch, not only HEAD's commits
    QToolButton *refreshButton_;
    std::optional<CommitLog> log_;
    QString error_; // why there is no log_
    std::optional<CommitDetails> details_;
    // Reading runs on pool_, in order (see ChangesView); generation_
    // changes with the folder, the path filter and the content search.
    QThreadPool pool_;
    int generation_ = 0;
    // Shared with the reading in progress (LogOptions::stop), which may
    // outlive its replacement.
    std::shared_ptr<std::atomic_bool> stopReading_ = std::make_shared<std::atomic_bool>(false);
    // The content search of the log read; the query's, once typed.
    std::optional<ContentSearch> contents_;
    bool searchingContents_ = false;
    QTimer contentsTimer_;
    bool reading_ = false;
    bool refreshPending_ = false;
    int commitLimit_ = 0; // commits to read, see loadMoreIfAtEnd()
    CommitDelegate *commitDelegate_;
    int graphLanes_ = 0;     // of the list's graph; 0 without one
    QString detailsWanted_;  // commit whose details are being read
    QString fileToKeep_;     // scrolled to again once the commit is read again
    QString unfoldedId_;     // the commit whose row shows its message; empty: none
    QString commitToReveal_; // selected after the next log read

    // The diff's share of the view's width.
    double diffShare_ = 0.6;
    bool isCommitListFolded_ = false;

    QLabel *summary_;
    QLabel *currentCommit_; // in the title bar, while the commits are folded
    HistorySearchField *filter_;
    ViewHeader *commitsHeader_;
    QTreeWidget *commits_;
    QWidget *commitListPane_ = nullptr; // the commits under their header
    FoldedPane *foldedCommits_;
    GripSplitter *splitter_; // the commits, the commit
    DiffHeaderBar *diffHeader_ = nullptr;
    DiffFileStrip *fileStrip_ = nullptr;
    DiffView *diff_ = nullptr;
    ChangeMap *changeMap_ = nullptr; // beside the diff
    FileLens *lens_ = nullptr;
    int blameGeneration_ = 0; // changes with each blame read
    QColor ruleColor_;
};
