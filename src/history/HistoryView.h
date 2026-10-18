#pragma once

#include "git/CommitLog.h"
#include "ui/Theme.h"

#include <QThreadPool>
#include <QWidget>

#include <optional>

class DiffView;
class QAction;
class QLabel;
class QLineEdit;
class QSplitter;
class QTextBrowser;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// "History" mode: commits of HEAD (those touching the open folder),
// filterable, across the whole view. Choosing a commit reveals its message
// and files below; choosing a file reveals on the right its diff against
// the first parent. The repository is only read.
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
    // Pane proportions, for the settings.
    QByteArray saveState() const;
    void restoreState(const QByteArray &state);

signals:
    // Path relative to the project folder.
    void openFileRequested(const QString &relativePath);
    // The history read by refresh() is shown.
    void refreshed();
    // The commit's context menu: check it out without a branch.
    void checkoutRequested(const QString &id);

private:
    void onLogRead(const std::optional<CommitLog> &log, const QString &error, int generation);
    void showLog(std::optional<CommitLog> log, const QString &error);
    void onDetailsRead(const QString &id, const std::optional<CommitDetails> &details, const QString &error);
    void rebuildList();
    void applyFilter();
    void onCommitSelectionChanged();
    void onFileSelectionChanged();
    void showCommit(const QString &id);
    void showDetails();
    void showPane(QSplitter *splitter, double &share);
    void hidePanes();
    void showCommitMenu(const QPoint &position);
    void fitMessage();

    QString folder_;
    QString pathFilter_;
    QToolButton *showAll_;
    QToolButton *refreshButton_;
    QAction *searchIcon_;
    std::optional<CommitLog> log_;
    QString error_; // why there is no log_
    std::optional<CommitDetails> details_;

    // Reading runs on pool_, in order (see ChangesView); generation_
    // changes with the folder and the path filter.
    QThreadPool pool_;
    int generation_ = 0;
    bool reading_ = false;
    bool refreshPending_ = false;
    QString detailsWanted_; // commit whose details are being read
    // Share of each splitter's second pane when it is visible.
    double panelShare_ = 0.5;
    double diffShare_ = 0.6;

    QLabel *summary_;
    QLineEdit *filter_;
    QTreeWidget *commits_;
    QTreeWidget *files_;
    QTextBrowser *message_;
    DiffView *diff_;
    QSplitter *splitter_;     // commits and commit pane on the left, diff on the right
    QSplitter *leftSplitter_; // commits above the commit pane
    QSplitter *panel_;        // message above the files
    QWidget *commitPane_;     // panel_ with its title bar
    QWidget *diffPane_;       // diff_ with its title bar
    QLabel *commitTitle_;
    QLabel *diffTitle_;
};
