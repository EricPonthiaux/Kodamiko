#include "HistoryView.h"

#include "CommitDelegate.h"
#include "CommitGraph.h"
#include "HistorySearchField.h"
#include "changes/DiffHeaderBar.h"
#include "changes/DiffView.h"
#include "ui/AppFonts.h"
#include "ui/GripSplitter.h"
#include "ui/UiIcons.h"

#include <QApplication>
#include <QClipboard>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>

#include <numeric>
#include <utility>

namespace {

constexpr int idRole = Qt::UserRole;
// From this width, the three panes side by side.
constexpr int wideLayoutWidth = 1180;
// Commits read at first; twice as many each time the end of the list comes
// into view, so that the walk, restarted each time, stays linear overall.
constexpr int firstCommitLimit = 500;
// A content search reads the whole history: started once typing pauses.
constexpr int contentsDelayMs = 600;

// "12 commits", "Last 500 commits"…
QString commitCountText(const CommitLog &log)
{
    if (log.info.unborn)
        return QObject::tr("No commits");
    if (log.truncated)
        return QObject::tr("Last %1 commits").arg(log.commits.size());
    if (log.commits.size() == 1)
        return QObject::tr("1 commit");
    return QObject::tr("%1 commits").arg(log.commits.size());
}

} // namespace

HistoryView::HistoryView(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , summary_(new QLabel(this))
    , filter_(new HistorySearchField(this))
    , commits_(new QTreeWidget(this))
    , splitter_(new GripSplitter(Qt::Horizontal, this))
    , leftSplitter_(new GripSplitter(Qt::Vertical, this))
{
    refreshButton_ = new QToolButton(this);
    refreshButton_->setToolTip(tr("Reload the history"));
    refreshButton_->setAutoRaise(true);
    refreshButton_->setFocusPolicy(Qt::NoFocus);
    connect(refreshButton_, &QToolButton::clicked, this, &HistoryView::refresh);

    connect(filter_, &HistorySearchField::queryChanged, this, &HistoryView::onQueryChanged);
    contentsTimer_.setSingleShot(true);
    contentsTimer_.setInterval(contentsDelayMs);
    connect(&contentsTimer_, &QTimer::timeout, this, &HistoryView::searchContents);

    allBranches_ = new QToolButton(this);
    allBranches_->setText(tr("All Branches"));
    allBranches_->setToolTip(tr("Show the commits of every branch, not only the current one"));
    allBranches_->setCheckable(true);
    allBranches_->setAutoRaise(true);
    allBranches_->setFocusPolicy(Qt::NoFocus);
    allBranches_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(allBranches_, &QToolButton::toggled, this, &HistoryView::restartReading);

    showAll_ = new QToolButton(this);
    showAll_->setText(tr("Show All"));
    showAll_->setToolTip(tr("Show the commits of every file again"));
    showAll_->setAutoRaise(true);
    showAll_->hide();
    connect(showAll_, &QToolButton::clicked, this, [this] { setPathFilter({}); });

    // "History" and the summary; the filter and the reload at the right.
    title_ = new QLabel(tr("History"), this);
    AppFonts::setInterfaceFont(title_, 1.15);
    summary_->setEnabled(false); // dimmed
    // The title bar, a line under it.
    auto *titleBar = new QFrame(this);
    titleBar->setObjectName(QStringLiteral("viewTitleBar"));
    // As tall as its contents: the panes below take the rest.
    titleBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *header = new QHBoxLayout(titleBar);
    header->setContentsMargins(16, 10, 12, 10);
    header->setSpacing(10);
    header->addWidget(title_);
    header->addWidget(summary_);
    header->addWidget(showAll_);
    header->addStretch();
    header->addWidget(filter_, 1);
    header->addWidget(allBranches_);
    header->addWidget(refreshButton_);

    // Commits on two lines: the summary and the branches and tags; the
    // avatar, author and date, and the ID.
    commits_->setObjectName(QStringLiteral("commits"));
    commits_->setHeaderHidden(true);
    commits_->setRootIsDecorated(false);
    commits_->setUniformRowHeights(true);
    commits_->setFrameShape(QFrame::NoFrame);
    commits_->setMouseTracking(true); // hover
    commits_->setContextMenuPolicy(Qt::CustomContextMenu);
    commitDelegate_ = new CommitDelegate(commits_);
    commits_->setItemDelegate(commitDelegate_);
    // The selection, not the current item: the view makes an item current
    // when it takes focus, without the user having chosen anything.
    connect(commits_, &QTreeWidget::itemSelectionChanged, this, &HistoryView::onCommitSelectionChanged);
    connect(commits_, &QTreeWidget::customContextMenuRequested, this, &HistoryView::showCommitMenu);
    // Scrolled down, or rows hidden by the filter: the end may come into view.
    connect(commits_->verticalScrollBar(), &QScrollBar::valueChanged, this, &HistoryView::loadMoreIfAtEnd);
    connect(commits_->verticalScrollBar(), &QScrollBar::rangeChanged, this, &HistoryView::loadMoreIfAtEnd);

    commitPane_ = createCommitPane();
    leftSplitter_->addWidget(commits_);
    leftSplitter_->addWidget(commitPane_);
    leftSplitter_->setChildrenCollapsible(false);
    splitter_->addWidget(leftSplitter_);
    splitter_->addWidget(createDiffPane());
    splitter_->setChildrenCollapsible(false);
    // Remembers the proportions set with the mouse.
    connect(splitter_, &QSplitter::splitterMoved, this, &HistoryView::rememberShares);
    connect(leftSplitter_, &QSplitter::splitterMoved, this, &HistoryView::rememberShares);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(titleBar);
    layout->addWidget(splitter_);
    showDetails();
    applyTheme(theme);
}

// The chosen file's diff under its header; a hint until one is chosen.
QWidget *HistoryView::createDiffPane()
{
    auto *pane = new QFrame(this);
    pane->setObjectName(QStringLiteral("historyPane"));
    diff_ = new DiffView(Theme::dark(), pane);
    diff_->setToolTip({});
    diffHeader_ = new DiffHeaderBar(diff_, pane);
    connect(diffHeader_, &DiffHeaderBar::whitespaceChanged, this, &HistoryView::rereadCommit);
    connect(diffHeader_, &DiffHeaderBar::heightChanged, this, &HistoryView::alignDiffHeaderRule);

    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(diffHeader_);
    layout->addWidget(diff_, 1);
    return pane;
}

void HistoryView::setFolder(const QString &folder)
{
    folder_ = folder;
    abandonReading();
    commitLimit_ = firstCommitLimit;
    setPathFilterShown({});
    log_.reset();
    error_.clear();
    rebuildList();
    forgetCommit();
    summary_->clear();
}

HistoryView::~HistoryView()
{
    stopReading_.request_stop();
    pool_.waitForDone();
}

void HistoryView::setPathFilter(const QString &relativePath)
{
    setPathFilterShown(relativePath);
    restartReading();
}

void HistoryView::abandonReading()
{
    ++generation_;
    stopReading_.request_stop();
    stopReading_ = std::stop_source();
}

void HistoryView::restartReading()
{
    abandonReading();
    commitLimit_ = firstCommitLimit;
    refresh();
}

void HistoryView::setPathFilterShown(const QString &relativePath)
{
    pathFilter_ = relativePath;
    const bool isFiltered = !relativePath.isEmpty();
    // A file, or a file deleted since: each commit shows only its diff.
    filtersOnFile_ = isFiltered && !QFileInfo(QDir(folder_).filePath(relativePath)).isDir();
    commitPane_->setVisible(!filtersOnFile_);
    title_->setText(isFiltered ? tr("History of %1").arg(QFileInfo(relativePath).fileName()) : tr("History"));
    title_->setToolTip(relativePath);
    showAll_->setVisible(isFiltered);
}

void HistoryView::refresh()
{
    if (folder_.isEmpty())
        return;
    if (reading_) {
        refreshPending_ = true;
        return;
    }
    reading_ = true;
    refreshPending_ = false;
    // A path filter or a content search walks the whole history: off the UI
    // thread.
    const LogOptions options{
        .limit = commitLimit_,
        .path = pathFilter_,
        .scope = allBranches_->isChecked() ? LogScope::AllBranches : LogScope::Head,
        .contents = contents_,
        .stop = stopReading_.get_token(),
    };
    pool_.start([this, folder = folder_, options, generation = generation_] {
        QString error;
        std::optional<CommitLog> log;
        try {
            log = CommitLog::read(folder, options);
            if (!log)
                error = tr("This folder is not in a git repository.");
        } catch (const GitError &exception) {
            error = QString::fromStdString(exception.what());
        }
        QMetaObject::invokeMethod(
            this, [this, log, error, generation] { onLogRead(log, error, generation); }, Qt::QueuedConnection);
    });
}

void HistoryView::onLogRead(const std::optional<CommitLog> &log, const QString &error, int generation)
{
    reading_ = false;
    if (generation != generation_ || refreshPending_) {
        refreshPending_ = false;
        refresh(); // outdated: read again
        return;
    }
    searchingContents_ = false;
    // Refreshed on every repository change: rebuilt only if it changed.
    if (log != log_ || error != error_)
        showLog(log, error);
    updateSummary();
    emit refreshed();
    loadMoreIfAtEnd(); // reached while reading
}

void HistoryView::loadMoreIfAtEnd()
{
    if (!log_ || !log_->truncated || reading_ || !commits_->isVisible())
        return;
    // While the list is rebuilt, its scroll bar says nothing of its end.
    if (commits_->topLevelItemCount() != log_->commits.size())
        return;
    if (!isEndInView() || isPastFirstDay())
        return;
    commitLimit_ *= 2;
    refresh();
}

bool HistoryView::isPastFirstDay() const
{
    const std::optional<QDate> after = filter_->query().after;
    return after && log_ && !log_->commits.isEmpty() && log_->commits.last().date.date() < *after;
}

bool HistoryView::isEndInView() const
{
    // The rows' geometry rather than the scroll bar's range, updated later.
    for (int i = commits_->topLevelItemCount() - 1; i >= 0; --i) {
        const QTreeWidgetItem *item = commits_->topLevelItem(i);
        if (!item->isHidden())
            return commits_->visualItemRect(item).top() < commits_->viewport()->height();
    }
    return true; // every row filtered out
}

void HistoryView::showLog(std::optional<CommitLog> log, const QString &error)
{
    error_ = error;
    const QList<QTreeWidgetItem *> selected = commits_->selectedItems();
    const QString current = selected.isEmpty() ? QString() : selected.first()->data(0, idRole).toString();
    // More commits read: the list grows under the eyes, it does not jump.
    const int scrolled = commits_->verticalScrollBar()->value();
    const QScopeGuard keepScroll([this, scrolled] { commits_->verticalScrollBar()->setValue(scrolled); });
    log_ = std::move(log);
    rebuildList();

    if (!log_) {
        forgetCommit();
        return;
    }
    // The chosen commit stays chosen if it still exists.
    for (int i = 0; i < commits_->topLevelItemCount(); ++i) {
        if (commits_->topLevelItem(i)->data(0, idRole).toString() == current) {
            commits_->setCurrentItem(commits_->topLevelItem(i));
            return;
        }
    }
    forgetCommit();
}

void HistoryView::rebuildList()
{
    const QSignalBlocker blocker(commits_);
    commits_->clear();
    if (!log_)
        return;
    QList<QTreeWidgetItem *> items;
    items.reserve(log_->commits.size());
    for (const CommitSummary &commit : std::as_const(log_->commits)) {
        auto *item = new QTreeWidgetItem;
        CommitDelegate::setCommit(item, commit);
        item->setData(0, idRole, commit.id);
        items.append(item);
    }
    // A commit filtered out breaks the lines between those kept: no graph.
    const bool showsEveryCommit = pathFilter_.isEmpty() && log_->info.prefix.isEmpty() && !contents_;
    const QList<GraphRow> graph = showsEveryCommit ? CommitGraph::layOut(log_->commits) : QList<GraphRow>();
    for (qsizetype i = 0; i < graph.size(); ++i)
        CommitDelegate::setGraphRow(items[i], graph[i]);
    graphLanes_ = CommitGraph::laneCount(graph);
    commits_->addTopLevelItems(items);
    applyFilter();
}

void HistoryView::onQueryChanged()
{
    applyFilter();
    // A search of the changes only once typing pauses; another query
    // cancels the one waiting.
    if (filter_->query().contents != contents_)
        contentsTimer_.start();
    else
        contentsTimer_.stop();
}

void HistoryView::searchContents()
{
    contents_ = filter_->query().contents;
    searchingContents_ = contents_.has_value() && !folder_.isEmpty();
    // The commits shown are not those searched: none until they are read.
    log_.reset();
    rebuildList();
    forgetCommit();
    restartReading();
    updateSummary();
}

void HistoryView::applyFilter()
{
    if (!log_)
        return; // no rows to filter
    const CommitQuery &query = filter_->query();
    for (int i = 0; i < commits_->topLevelItemCount(); ++i)
        commits_->topLevelItem(i)->setHidden(!query.matches(log_->commits.at(i)));
    // Rows hidden by the filter would break its lines too.
    commitDelegate_->setGraphLanes(query.keepsEveryCommit() ? graphLanes_ : 0);
    commits_->viewport()->update();
}

void HistoryView::updateSummary()
{
    if (searchingContents_) {
        summary_->setText(tr("Searching the changes…"));
        return;
    }
    if (!log_) {
        summary_->setText(error_);
        return;
    }
    QString summary = commitCountText(*log_);
    if (contents_)
        summary += tr(" changing \"%1\"").arg(contents_->pattern);
    if (!pathFilter_.isEmpty())
        summary += tr(" touching %1").arg(pathFilter_);
    else if (!log_->info.prefix.isEmpty())
        summary += tr(" touching %1").arg(log_->info.prefix);
    summary_->setText(summary);
}

void HistoryView::onCommitSelectionChanged()
{
    const QList<QTreeWidgetItem *> selected = commits_->selectedItems();
    if (selected.isEmpty())
        forgetCommit();
    else
        showCommit(selected.first()->data(0, idRole).toString());
}

void HistoryView::showCommitMenu(const QPoint &position)
{
    QTreeWidgetItem *item = commits_->itemAt(position);
    if (!item || !log_)
        return;
    const CommitSummary &commit = log_->commits.at(commits_->indexOfTopLevelItem(item));
    QMenu menu;
    menu.addAction(tr("Copy ID"), this, [id = commit.id] { QApplication::clipboard()->setText(id); });
    menu.addAction(tr("Copy Short ID"), this, [id = commit.shortId] { QApplication::clipboard()->setText(id); });
    menu.addAction(tr("Copy Summary"), this,
                   [summary = commit.summary] { QApplication::clipboard()->setText(summary); });
    menu.addSeparator();
    menu.addAction(tr("Check Out This Commit…"), this, [this, id = commit.shortId] { emit checkoutRequested(id); });
    menu.exec(commits_->viewport()->mapToGlobal(position));
}

void HistoryView::focusFilter()
{
    filter_->setFocus();
    filter_->selectAll();
}

void HistoryView::applyTheme(const Theme &theme)
{
    ruleColor_ = theme.line;
    diff_->applyTheme(theme);
    diffHeader_->applyTheme(theme);
    refreshButton_->setIcon(UiIcons::icon(QStringLiteral("refresh"), theme.foreground));
    allBranches_->setIcon(UiIcons::icon(QStringLiteral("branch"), theme.foreground));
    filter_->applyTheme(theme);
    copyId_->setIcon(UiIcons::icon(QStringLiteral("copy"), theme.mutedText));
    commitId_->setFont(AppFonts::codeFont(font().pointSizeF() * 0.95));
    alignDiffHeaderRule();
}

void HistoryView::alignDiffHeaderRule()
{
    // The hint can wrap as the pane narrows; its rule follows the header.
    splitter_->setHandleRule(splitter_->indexOf(diffHeader_->parentWidget()),
                             {diffHeader_->height() - 1, Qt::RightEdge, ruleColor_});
}

void HistoryView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    arrangePanes();
    fitMessage(); // its width changed
}

void HistoryView::arrangePanes()
{
    const bool wide = width() >= wideLayoutWidth;
    if (wide == wide_ && commitPane_->parentWidget() == (wide ? splitter_ : leftSplitter_))
        return;
    arranging_ = true;
    wide_ = wide;
    if (wide)
        splitter_->insertWidget(1, commitPane_);
    else
        leftSplitter_->insertWidget(1, commitPane_);
    commitPane_->setVisible(!filtersOnFile_);
    applyShares();
    alignDiffHeaderRule();
    arranging_ = false;
}

void HistoryView::applyShares()
{
    const int total = splitter_->width();
    if (total <= 0)
        return;
    const int diff = qRound(total * diffShare_);
    const int rest = total - diff;
    if (wide_) {
        const int commit = qRound(rest * commitShare_);
        splitter_->setSizes({rest - commit, commit, diff});
        return;
    }
    splitter_->setSizes({rest, diff});
    const int height = leftSplitter_->height();
    const int commit = qRound(height * commitShare_);
    leftSplitter_->setSizes({height - commit, commit});
}

void HistoryView::rememberShares()
{
    if (arranging_)
        return;
    const QList<int> sizes = splitter_->sizes();
    const int total = std::accumulate(sizes.begin(), sizes.end(), 0);
    if (total <= 0 || sizes.last() <= 0)
        return;
    diffShare_ = double(sizes.last()) / total;
    const QList<int> commitSizes = wide_ ? sizes.mid(0, 2) : leftSplitter_->sizes();
    const int both = commitSizes.value(0) + commitSizes.value(1);
    if (both > 0 && commitSizes.value(1) > 0)
        commitShare_ = double(commitSizes.value(1)) / both;
}

QByteArray HistoryView::saveState() const
{
    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    // The empty array once held a splitter's state: the format stays readable.
    stream << commitShare_ << diffShare_ << QByteArray();
    return state;
}

void HistoryView::restoreState(const QByteArray &state)
{
    QDataStream stream(state);
    double commitShare = 0;
    double diffShare = 0;
    QByteArray unused;
    stream >> commitShare >> diffShare >> unused;
    // Also rejects the old format (raw QSplitter state).
    if (stream.status() != QDataStream::Ok || commitShare <= 0 || commitShare >= 1 || diffShare <= 0 || diffShare >= 1)
        return;
    commitShare_ = commitShare;
    diffShare_ = diffShare;
    applyShares();
}
