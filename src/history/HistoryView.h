// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/CommitLog.h"
#include "ui/Theme.h"

#include <QColor>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <memory>
#include <optional>

class CommitDelegate;
class DiffHeaderBar;
class DiffView;
class FoldedPane;
class QLabel;
class GripSplitter;
class HistorySearchField;
class QSplitter;
class QStackedWidget;
class QTextBrowser;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class ViewHeader;

// "History" mode: commits of HEAD (those touching the open folder),
// searchable (HistorySearchField): the commits read are filtered as the
// query is typed; a search of their changes reads the history again. Three panes that stay put whatever is chosen: the
// commits; the chosen commit (its summary, author, date and ID, its message, its files); the diff of the chosen file
// against the first parent. A narrow window puts the commit under the list. The commits and the commit
// each fold away into a thin bar at the left edge: the commits folded, the title bar names the chosen
// commit; the commit folded, the diff shows all its files. The repository is only read.
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
    // Only the commits that modify `relativePath` (a file or folder of the
    // open folder); empty shows them all again. Reloads the history.
    void setPathFilter(const QString &relativePath);
    void applyTheme(const Theme &theme);

    // Whether the three panes are side by side (a wide window).
    bool isWide() const { return wide_; }

    // The commits, and the chosen commit's pane, folded away. Neither at first.
    void setCommitListFolded(bool folded);
    void setCommitPaneFolded(bool folded);

    bool isCommitListFolded() const { return isCommitListFolded_; }

    bool isCommitPaneFolded() const { return isCommitPaneFolded_; }

    // Pane proportions and folds, for the settings.
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

private:
    QWidget *createCommitListPane();
    QWidget *createCommitPane();
    QWidget *createDiffPane();
    // The bars at the top of the panes take the same height, and the
    // handle before the diff carries its header's rule.
    void alignBars();
    // The commit's pane is neither folded nor hidden by a file's history.
    bool showsCommitPane() const;
    // Shows the panes left unfolded, and the bars of those folded.
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
    // The chosen commit again, as the header's whitespace button says; its
    // chosen file stays chosen.
    void rereadCommit();
    // The file or folder whose changes commit `commitId` shows: the path
    // filter, or the name the filtered file had before a rename.
    QString filterOf(const QString &commitId) const;
    // Shows the diff of the commit's file at `path`, if listed.
    void chooseFile(const QString &path);
    void rebuildList();
    void onQueryChanged();
    // The query's content search, typed a moment ago: the history read again.
    void searchContents();
    void applyFilter();
    void updateSummary();
    void onCommitSelectionChanged();
    void onFileSelectionChanged();
    void showCommit(const QString &id);
    void forgetCommit();
    QString diffPlaceholder() const;
    // The filter, named in the title, without reloading.
    void setPathFilterShown(const QString &relativePath);
    void showDetails();
    // The message's body, its other details: a few lines, all of them once
    // expanded.
    QString messageHtml(const CommitDetails &details) const;
    void fitMessage();
    void showCommitMenu(const QPoint &position);
    // Side by side, or the commit under the list, for the view's width.
    void arrangePanes();
    // The panes' sizes from their shares; the diff takes those of folded panes.
    void applyShares();
    void rememberShares();

    QString folder_;
    QString pathFilter_;
    bool filtersOnFile_ = false; // the commit pane hidden: only the file's diff
    QLabel *title_;              // names the filtered file
    QToolButton *showAll_;
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
    int graphLanes_ = 0;    // of the list's graph; 0 without one
    QString detailsWanted_; // commit whose details are being read
    QString fileToKeep_;    // chosen again once the commit is read again

    // The commit pane's share of the commits and the commit, the diff's of
    // the whole: the same in both arrangements.
    double commitShare_ = 0.45;
    double diffShare_ = 0.45;
    bool wide_ = false;
    bool arranging_ = false; // splitterMoved, while the panes are placed
    bool isCommitListFolded_ = false;
    bool isCommitPaneFolded_ = false;

    QLabel *summary_;
    QLabel *currentCommit_; // in the title bar, while the commits are folded
    HistorySearchField *filter_;
    ViewHeader *commitsHeader_;
    QTreeWidget *commits_;
    QWidget *commitListPane_ = nullptr; // the commits under their header
    FoldedPane *foldedCommits_;
    FoldedPane *foldedCommit_;
    GripSplitter *splitter_;  // the commits (and the commit when narrow), the commit, the diff
    QSplitter *leftSplitter_; // the commits, the commit under them when narrow
    QWidget *commitPane_ = nullptr;
    ViewHeader *commitHeader_ = nullptr;
    QStackedWidget *commitPages_ = nullptr; // a hint, or the chosen commit
    QLabel *commitSummary_ = nullptr;
    QLabel *avatar_ = nullptr;
    QLabel *commitMeta_ = nullptr;
    QLabel *commitId_ = nullptr;
    QToolButton *copyId_ = nullptr;
    QTextBrowser *message_ = nullptr;
    QToolButton *moreMessage_ = nullptr;
    bool messageExpanded_ = false;
    QLabel *commitStats_ = nullptr;
    QTreeWidget *files_ = nullptr;
    DiffHeaderBar *diffHeader_ = nullptr;
    DiffView *diff_ = nullptr;
    QColor ruleColor_;
};
