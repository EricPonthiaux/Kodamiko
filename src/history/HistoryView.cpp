#include "HistoryView.h"

#include "CommitDelegate.h"
#include "changes/DiffHeaderBar.h"
#include "changes/DiffView.h"
#include "changes/FileChangeDelegate.h"
#include "git/ChangeKindStyle.h"
#include "ui/AppFonts.h"
#include "ui/GripSplitter.h"
#include "ui/UiIcons.h"

#include <QApplication>
#include <QClipboard>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>
#include <numeric>
#include <utility>

namespace {

constexpr int idRole = Qt::UserRole;
constexpr int fileIndexRole = Qt::UserRole;
// From this width, the three panes side by side.
constexpr int wideLayoutWidth = 1180;
// The message shows this many lines until expanded.
constexpr int collapsedMessageLines = 5;
constexpr int avatarSize = 18;
// Commits read at first; twice as many each time the end of the list comes
// into view, so that the walk, restarted each time, stays linear overall.
constexpr int firstCommitLimit = 500;

QString formatDate(const QDateTime &date)
{
    return QLocale().toString(date.toLocalTime(), QLocale::ShortFormat);
}

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

// Whether `change` is the file or under the folder `filter` (an empty
// filter: every file), by its path or the one it was renamed from.
bool isFiltered(const FileChange &change, const QString &filter)
{
    const auto isUnder = [&filter](const QString &path) {
        return path == filter || path.startsWith(filter + QLatin1Char('/'));
    };
    return filter.isEmpty() || isUnder(change.path) || (!change.oldPath.isEmpty() && isUnder(change.oldPath));
}

QLabel *mutedLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("historyMuted"));
    return label;
}

} // namespace

HistoryView::HistoryView(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , summary_(new QLabel(this))
    , filter_(new QLineEdit(this))
    , commits_(new QTreeWidget(this))
    , splitter_(new GripSplitter(Qt::Horizontal, this))
    , leftSplitter_(new GripSplitter(Qt::Vertical, this))
{
    refreshButton_ = new QToolButton(this);
    refreshButton_->setToolTip(tr("Reload the history"));
    refreshButton_->setAutoRaise(true);
    refreshButton_->setFocusPolicy(Qt::NoFocus);
    connect(refreshButton_, &QToolButton::clicked, this, &HistoryView::refresh);

    filter_->setPlaceholderText(tr("Filter: message, author, ID"));
    filter_->setClearButtonEnabled(true);
    filter_->setMaximumWidth(420);
    searchIcon_ = filter_->addAction(QIcon(), QLineEdit::LeadingPosition);
    connect(filter_, &QLineEdit::textChanged, this, &HistoryView::applyFilter);

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
    commits_->setItemDelegate(new CommitDelegate(commits_));
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

// A hint until a commit is chosen; then its summary in demibold, the
// author, date and ID on one line (the ID with a copy button), the message
// (a few lines until expanded), the counts, and the files.
QWidget *HistoryView::createCommitPane()
{
    auto *pane = new QFrame(this);
    pane->setObjectName(QStringLiteral("historyPane"));
    commitPages_ = new QStackedWidget(pane);

    auto *hint = mutedLabel(commitPages_);
    hint->setText(tr("Choose a commit to see its message and files."));
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    commitPages_->addWidget(hint);

    auto *details = new QWidget(commitPages_);
    commitSummary_ = new QLabel(details);
    AppFonts::setInterfaceFont(commitSummary_, 1.05, QFont::DemiBold);
    commitSummary_->setWordWrap(true);
    commitSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    avatar_ = new QLabel(details);
    commitMeta_ = mutedLabel(details);
    commitId_ = mutedLabel(details);
    commitId_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    copyId_ = new QToolButton(details);
    copyId_->setAutoRaise(true);
    copyId_->setFocusPolicy(Qt::NoFocus);
    copyId_->setToolTip(tr("Copy the commit's ID"));
    connect(copyId_, &QToolButton::clicked, this, [this] {
        if (details_)
            QApplication::clipboard()->setText(details_->summary.id);
    });
    auto *meta = new QHBoxLayout;
    meta->setSpacing(6);
    meta->addWidget(avatar_);
    meta->addWidget(commitMeta_, 1);
    meta->addWidget(commitId_);
    meta->addWidget(copyId_);

    message_ = new QTextBrowser(details);
    message_->setOpenLinks(false);
    message_->setFrameShape(QFrame::NoFrame);
    // Part of the pane, not a field: no border of its own.
    message_->setStyleSheet(QStringLiteral("QTextBrowser { border: none; border-radius: 0; padding: 0; }"));
    message_->document()->setDocumentMargin(0);
    moreMessage_ = new QToolButton(details);
    moreMessage_->setObjectName(QStringLiteral("linkButton"));
    moreMessage_->setAutoRaise(true);
    moreMessage_->setFocusPolicy(Qt::NoFocus);
    connect(moreMessage_, &QToolButton::clicked, this, [this] {
        messageExpanded_ = !messageExpanded_;
        fitMessage();
    });
    commitStats_ = new QLabel(details);

    files_ = new QTreeWidget(details);
    files_->setObjectName(QStringLiteral("files"));
    files_->setHeaderHidden(true);
    files_->setRootIsDecorated(false);
    files_->setUniformRowHeights(true);
    files_->setFrameShape(QFrame::NoFrame);
    files_->setMouseTracking(true);
    files_->setItemDelegate(new FileChangeDelegate(files_));
    files_->setToolTip(tr("Double-click: open the file in the editor"));
    connect(files_, &QTreeWidget::itemSelectionChanged, this, &HistoryView::onFileSelectionChanged);
    connect(files_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if (!details_)
            return;
        const FileChange &change = details_->files.at(item->data(0, fileIndexRole).toInt()).change;
        if (QFileInfo(QDir(folder_).filePath(change.path)).isFile())
            emit openFileRequested(change.path);
    });

    auto *text = new QVBoxLayout;
    text->setContentsMargins(12, 10, 12, 6);
    text->setSpacing(6);
    text->addWidget(commitSummary_);
    text->addLayout(meta);
    text->addWidget(message_);
    text->addWidget(moreMessage_, 0, Qt::AlignLeft);
    text->addWidget(commitStats_);
    auto *detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(0);
    detailsLayout->addLayout(text);
    detailsLayout->addWidget(files_, 1);
    commitPages_->addWidget(details);

    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(commitPages_);
    return pane;
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
    ++generation_;
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
    pool_.waitForDone();
}

void HistoryView::setPathFilter(const QString &relativePath)
{
    setPathFilterShown(relativePath);
    ++generation_;
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
    // A path filter walks the whole history: off the UI thread.
    pool_.start([this, folder = folder_, path = pathFilter_, limit = commitLimit_, generation = generation_] {
        QString error;
        std::optional<CommitLog> log;
        try {
            log = CommitLog::read(folder, limit, path);
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
    // Refreshed on every repository change: rebuilt only if it changed.
    if (log != log_ || error != error_)
        showLog(log, error);
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
    if (!isEndInView())
        return;
    commitLimit_ *= 2;
    refresh();
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
        summary_->setText(error);
        forgetCommit();
        return;
    }
    QString summary = commitCountText(*log_);
    if (!pathFilter_.isEmpty())
        summary += tr(" touching %1").arg(pathFilter_);
    else if (!log_->info.prefix.isEmpty())
        summary += tr(" touching %1").arg(log_->info.prefix);
    summary_->setText(summary);

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
    commits_->addTopLevelItems(items);
    applyFilter();
}

void HistoryView::applyFilter()
{
    if (!log_)
        return; // no rows to filter
    const QString text = filter_->text().trimmed();
    for (int i = 0; i < commits_->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = commits_->topLevelItem(i);
        const CommitSummary &commit = log_->commits.at(i);
        const bool match =
            text.isEmpty() || commit.summary.contains(text, Qt::CaseInsensitive) ||
            commit.author.contains(text, Qt::CaseInsensitive) || commit.email.contains(text, Qt::CaseInsensitive) ||
            commit.id.startsWith(text, Qt::CaseInsensitive) || commit.refs.contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void HistoryView::onCommitSelectionChanged()
{
    const QList<QTreeWidgetItem *> selected = commits_->selectedItems();
    if (selected.isEmpty())
        forgetCommit();
    else
        showCommit(selected.first()->data(0, idRole).toString());
}

void HistoryView::onFileSelectionChanged()
{
    const QList<QTreeWidgetItem *> selected = files_->selectedItems();
    if (selected.isEmpty() || !details_) {
        diffHeader_->setPlaceholder(diffPlaceholder());
        diff_->showMessage({});
        return;
    }
    const FileDiff &file = details_->files.at(selected.first()->data(0, fileIndexRole).toInt());
    diff_->setFiles({{file.change.path, changeStatusText(file.change), FilePatch::parse(file.patch), file.isTooLarge,
                      file.newText}});
}

QString HistoryView::filterOf(const QString &commitId) const
{
    if (!log_)
        return pathFilter_;
    const auto commit = std::ranges::find_if(log_->commits, [&commitId](const CommitSummary &summary) {
        return summary.id == commitId && !summary.path.isEmpty();
    });
    return commit != log_->commits.end() ? commit->path : pathFilter_;
}

void HistoryView::chooseFile(const QString &path)
{
    if (!details_)
        return;
    for (int i = 0; i < files_->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = files_->topLevelItem(i);
        if (details_->files.at(item->data(0, fileIndexRole).toInt()).change.path == path) {
            files_->setCurrentItem(item);
            return;
        }
    }
}

// What to choose for a diff to show; nothing before a commit is chosen,
// unless the commit pane, which says it, is hidden.
QString HistoryView::diffPlaceholder() const
{
    if (details_)
        return tr("Choose a file of the commit");
    return filtersOnFile_ ? tr("Choose a commit to see its changes to %1").arg(pathFilter_) : QString();
}

void HistoryView::forgetCommit()
{
    details_.reset();
    detailsWanted_.clear();
    showDetails();
}

// Every patch of a large commit: read off the UI thread.
void HistoryView::showCommit(const QString &id)
{
    if ((details_ && details_->summary.id == id) || detailsWanted_ == id)
        return;
    detailsWanted_ = id;
    pool_.start([this, folder = folder_, id, whitespace = diffHeader_->whitespace()] {
        std::optional<CommitDetails> details;
        QString error;
        try {
            details = CommitDetails::read(folder, id, whitespace);
        } catch (const GitError &exception) {
            error = QString::fromStdString(exception.what());
        }
        QMetaObject::invokeMethod(
            this, [this, id, whitespace, details, error] { onDetailsRead({id, whitespace}, details, error); },
            Qt::QueuedConnection);
    });
}

void HistoryView::rereadCommit()
{
    const QString id = details_ ? details_->summary.id : detailsWanted_;
    if (id.isEmpty())
        return;
    if (const QTreeWidgetItem *item = files_->currentItem(); item && details_)
        fileToKeep_ = details_->files.at(item->data(0, fileIndexRole).toInt()).change.path;
    details_.reset();
    detailsWanted_.clear();
    showCommit(id);
}

void HistoryView::onDetailsRead(const DetailsRequest &request,
                                const std::optional<CommitDetails> &details,
                                const QString &error)
{
    // Another commit was chosen meanwhile, or whitespace toggled.
    if (request.id != detailsWanted_ || request.whitespace != diffHeader_->whitespace())
        return;
    detailsWanted_.clear();
    details_ = details;
    showDetails();
    if (!details) {
        auto *hint = static_cast<QLabel *>(commitPages_->widget(0));
        hint->setText(error);
    }
}

void HistoryView::showDetails()
{
    diff_->hideUnchangedLines(); // those of the previous commit
    files_->clear();
    onFileSelectionChanged(); // nothing chosen in the new list
    auto *hint = static_cast<QLabel *>(commitPages_->widget(0));
    hint->setText(tr("Choose a commit to see its message and files."));
    if (!details_) {
        commitPages_->setCurrentIndex(0);
        message_->clear();
        return;
    }

    const CommitDetails &details = *details_;
    const CommitSummary &commit = details.summary;
    commitSummary_->setText(commit.summary);
    avatar_->setPixmap(CommitDelegate::avatar(commit.author, avatarSize, devicePixelRatioF()));
    commitMeta_->setText(QStringLiteral("%1 · %2").arg(commit.author, CommitDelegate::relativeDate(commit.date)));
    commitMeta_->setToolTip(QStringLiteral("%1 <%2>\n%3").arg(commit.author, commit.email, formatDate(commit.date)));
    commitId_->setText(commit.shortId);
    commitId_->setToolTip(commit.id);
    messageExpanded_ = false;
    message_->setHtml(messageHtml(details));

    // "4 files changed  +120 −8", above the files.
    QList<FilePatch> patches;
    int added = 0;
    int removed = 0;
    for (const FileDiff &file : details.files) {
        patches.append(FilePatch::parse(file.patch));
        added += patches.last().addedLines();
        removed += patches.last().removedLines();
    }
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    commitStats_->setText(
        QStringLiteral("<span style='color:%1'>%2</span>&nbsp;&nbsp; <span style='color:%3; font-weight:600'>+%4</span>"
                       "&nbsp; <span style='color:%5; font-weight:600'>−%6</span>")
            .arg(palette().color(QPalette::PlaceholderText).name(),
                 details.files.size() == 1 ? tr("1 file changed") : tr("%1 files changed").arg(details.files.size()),
                 dark ? QStringLiteral("#3fb950") : QStringLiteral("#1a7f37"), QString::number(added),
                 dark ? QStringLiteral("#f85149") : QStringLiteral("#cf222e"), QString::number(removed)));
    // Only the filtered file or folder: no need to look for it.
    const QString filter = filterOf(commit.id);
    for (int i = 0; i < details.files.size(); ++i) {
        const FileDiff &file = details.files[i];
        if (!isFiltered(file.change, filter))
            continue;
        auto *item = new QTreeWidgetItem(files_);
        FileChangeDelegate::setChange(item, file.change);
        FileChangeDelegate::setStats(item, patches[i]);
        item->setData(0, fileIndexRole, i);
    }
    if (!pathFilter_.isEmpty() && files_->topLevelItemCount() == 1)
        files_->setCurrentItem(files_->topLevelItem(0)); // the filtered file's diff right away
    if (!fileToKeep_.isEmpty())
        chooseFile(std::exchange(fileToKeep_, {}));
    commitPages_->setCurrentIndex(1);
    // Once the pane is laid out.
    QTimer::singleShot(0, this, &HistoryView::fitMessage);
}

QString HistoryView::messageHtml(const CommitDetails &details) const
{
    const CommitSummary &commit = details.summary;
    const QString muted = palette().color(QPalette::PlaceholderText).name();
    QString html;
    const QString body = details.message.section(QLatin1Char('\n'), 1).trimmed();
    if (!body.isEmpty())
        html += QStringLiteral("<p style='white-space:pre-wrap; margin:0 0 8px 0'>%1</p>").arg(body.toHtmlEscaped());
    QStringList notes{commit.email.toHtmlEscaped() + QStringLiteral(" · ") + formatDate(commit.date)};
    if (!details.committer.isEmpty())
        notes.append(
            tr("Committed by %1 on %2").arg(details.committer.toHtmlEscaped(), formatDate(details.commitDate)));
    if (details.parentIds.size() > 1)
        notes.append(tr("Merge of %1 — diff against %2")
                         .arg(details.parentIds.join(QStringLiteral(", ")), details.parentIds.first()));
    html += QStringLiteral("<p style='color:%1; margin:0'>%2</p>").arg(muted, notes.join(QStringLiteral("<br>")));
    if (!commit.refs.isEmpty()) {
        QStringList refs;
        for (const QString &ref : commit.refs)
            refs.append(QStringLiteral("<span style='color:%1; font-weight:600'>%2</span>")
                            .arg(CommitDelegate::refColor(ref).lighter(125).name(), ref.toHtmlEscaped()));
        html += QStringLiteral("<p style='margin:6px 0 0 0'>%1</p>").arg(refs.join(QStringLiteral("&nbsp;&nbsp; ")));
    }
    return html;
}

// As tall as the message, up to a few lines until expanded.
void HistoryView::fitMessage()
{
    if (!details_)
        return;
    message_->document()->setTextWidth(message_->viewport()->width());
    const int wanted = qCeil(message_->document()->size().height()) + 2;
    const int collapsed = message_->fontMetrics().lineSpacing() * collapsedMessageLines;
    const bool isLong = wanted > collapsed;
    moreMessage_->setVisible(isLong);
    moreMessage_->setText(messageExpanded_ ? tr("Show less") : tr("Show more"));
    // Expanded, still leaving the files room.
    const int expanded = std::min(wanted, std::max(collapsed, commitPane_->height() / 2));
    const bool isCollapsed = isLong && !messageExpanded_;
    message_->setFixedHeight(isCollapsed ? collapsed : expanded);
    // Cut, it reads as cut: "Show more" says the rest is there.
    message_->setVerticalScrollBarPolicy(isCollapsed ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
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
    diff_->applyTheme(theme);
    diffHeader_->applyTheme(theme);
    refreshButton_->setIcon(UiIcons::icon(QStringLiteral("refresh"), theme.foreground));
    searchIcon_->setIcon(UiIcons::icon(QStringLiteral("search"), theme.mutedText));
    copyId_->setIcon(UiIcons::icon(QStringLiteral("copy"), theme.mutedText));
    commitId_->setFont(AppFonts::codeFont(font().pointSizeF() * 0.95));
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
