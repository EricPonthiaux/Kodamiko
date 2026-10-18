#include "HistoryView.h"

#include "changes/DiffView.h"
#include "changes/FileChangeDelegate.h"
#include "CommitDelegate.h"
#include "git/ChangeKindStyle.h"
#include "ui/AppFonts.h"
#include "ui/GripSplitter.h"
#include "ui/UiIcons.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QDataStream>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>

#include <functional>

namespace {

constexpr int idRole = Qt::UserRole;
constexpr int fileIndexRole = Qt::UserRole;

QString formatDate(const QDateTime &date)
{
    return QLocale().toString(date.toLocalTime(), QLocale::ShortFormat);
}

// Closable pane: a bar with its title and a close button, above the content.
QWidget *closablePane(QLabel *title, QWidget *content, const QString &closeTip, QWidget *parent,
                      const std::function<void()> &onClose)
{
    auto *pane = new QWidget(parent);
    auto *close = new QToolButton(pane);
    close->setText(QStringLiteral("✕"));
    close->setToolTip(closeTip);
    close->setAutoRaise(true);
    QObject::connect(close, &QToolButton::clicked, pane, onClose);

    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    AppFonts::setInterfaceFont(title, 1);
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(14, 6, 6, 6);
    bar->addWidget(title, 1);
    bar->addWidget(close);

    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(bar);
    layout->addWidget(content, 1);
    return pane;
}

} // namespace

HistoryView::HistoryView(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , summary_(new QLabel(this))
    , filter_(new QLineEdit(this))
    , commits_(new QTreeWidget(this))
    , files_(new QTreeWidget(this))
    , message_(new QTextBrowser(this))
    , diff_(new DiffView(theme, this))
    , splitter_(new GripSplitter(Qt::Horizontal, this))
    , leftSplitter_(new GripSplitter(Qt::Vertical, this))
    , panel_(new GripSplitter(Qt::Vertical, this))
    , commitTitle_(new QLabel(this))
    , diffTitle_(new QLabel(this))
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
    auto *title = new QLabel(tr("History"), this);
    AppFonts::setInterfaceFont(title, 1.15);
    summary_->setEnabled(false); // dimmed
    auto *header = new QHBoxLayout;
    header->setContentsMargins(16, 10, 12, 10);
    header->setSpacing(10);
    header->addWidget(title);
    header->addWidget(summary_);
    header->addWidget(showAll_);
    header->addStretch();
    header->addWidget(filter_, 1);
    header->addWidget(refreshButton_);

    // Commits on two lines: avatar, branches and tags, summary; author, date, ID.
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

    // Files of the commit: choosing one shows its diff on the right.
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

    message_->setOpenLinks(false);
    message_->setFrameShape(QFrame::NoFrame);
    // Part of the pane, not a field: no border of its own.
    message_->setStyleSheet(QStringLiteral("QTextBrowser { border: none; border-radius: 0; padding: 0; }"));
    message_->document()->setDocumentMargin(14);
    diff_->setToolTip({});

    panel_->addWidget(message_);
    panel_->addWidget(files_);
    panel_->setStretchFactor(1, 1);
    panel_->setSizes({210, 180});

    // A pane's close button deselects whatever made it appear, which hides it.
    commitPane_ = closablePane(commitTitle_, panel_, tr("Close commit"), this, [this] { commits_->clearSelection(); });
    diffPane_ = closablePane(diffTitle_, diff_, tr("Close diff"), this, [this] { files_->clearSelection(); });

    leftSplitter_->addWidget(commits_);
    leftSplitter_->addWidget(commitPane_);
    leftSplitter_->setChildrenCollapsible(false);

    splitter_->addWidget(leftSplitter_);
    splitter_->addWidget(diffPane_);
    splitter_->setChildrenCollapsible(false);

    // Remembers the proportions set with the mouse, for the next time it appears.
    const auto trackShare = [](QSplitter *splitter, double &share) {
        connect(splitter, &QSplitter::splitterMoved, splitter, [splitter, &share] {
            const QList<int> sizes = splitter->sizes();
            if (sizes.value(0) > 0 && sizes.value(1) > 0)
                share = double(sizes[1]) / (sizes[0] + sizes[1]);
        });
    };
    trackShare(leftSplitter_, panelShare_);
    trackShare(splitter_, diffShare_);

    // Initially, the commit list alone.
    hidePanes();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(splitter_);
    applyTheme(theme);
}

void HistoryView::setFolder(const QString &folder)
{
    folder_ = folder;
    ++generation_;
    pathFilter_.clear();
    showAll_->hide();
    log_.reset();
    error_.clear();
    details_.reset();
    detailsWanted_.clear();
    rebuildList();
    showDetails();
    hidePanes();
    summary_->clear();
}

HistoryView::~HistoryView()
{
    pool_.waitForDone();
}

void HistoryView::setPathFilter(const QString &relativePath)
{
    pathFilter_ = relativePath;
    ++generation_;
    showAll_->setVisible(!relativePath.isEmpty());
    refresh();
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
    pool_.start([this, folder = folder_, path = pathFilter_, generation = generation_] {
        QString error;
        std::optional<CommitLog> log;
        try {
            log = CommitLog::read(folder, 2000, path);
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
}

void HistoryView::showLog(std::optional<CommitLog> log, const QString &error)
{
    error_ = error;
    const QList<QTreeWidgetItem *> selected = commits_->selectedItems();
    const QString current = selected.isEmpty() ? QString() : selected.first()->data(0, idRole).toString();
    log_ = std::move(log);
    rebuildList();

    if (!log_) {
        summary_->setText(error);
        details_.reset();
        detailsWanted_.clear();
        showDetails();
        hidePanes();
        return;
    }
    QString summary = log_->info.unborn ? tr("No commits")
                      : log_->truncated  ? tr("Last %1 commits").arg(log_->commits.size())
                      : log_->commits.size() == 1 ? tr("1 commit")
                                                  : tr("%1 commits").arg(log_->commits.size());
    if (!pathFilter_.isEmpty())
        summary += tr(" touching %1").arg(pathFilter_);
    else if (!log_->info.prefix.isEmpty())
        summary += tr(" touching %1").arg(log_->info.prefix);
    summary_->setText(summary);

    // The chosen commit stays chosen if it still exists; otherwise, back to the list alone.
    for (int i = 0; i < commits_->topLevelItemCount(); ++i) {
        if (commits_->topLevelItem(i)->data(0, idRole).toString() == current) {
            commits_->setCurrentItem(commits_->topLevelItem(i));
            return;
        }
    }
    details_.reset();
    detailsWanted_.clear();
    showDetails();
    hidePanes();
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
    const QString text = filter_->text().trimmed();
    for (int i = 0; i < commits_->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = commits_->topLevelItem(i);
        const CommitSummary &commit = log_->commits.at(i);
        const bool match = text.isEmpty() || commit.summary.contains(text, Qt::CaseInsensitive)
                           || commit.author.contains(text, Qt::CaseInsensitive)
                           || commit.email.contains(text, Qt::CaseInsensitive) || commit.id.startsWith(text, Qt::CaseInsensitive)
                           || commit.refs.contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void HistoryView::onCommitSelectionChanged()
{
    const QList<QTreeWidgetItem *> selected = commits_->selectedItems();
    if (selected.isEmpty()) {
        detailsWanted_.clear();
        hidePanes();
        return;
    }
    showCommit(selected.first()->data(0, idRole).toString());
    showPane(leftSplitter_, panelShare_);
}

void HistoryView::onFileSelectionChanged()
{
    const QList<QTreeWidgetItem *> selected = files_->selectedItems();
    if (selected.isEmpty() || !details_) {
        diffPane_->hide();
        return;
    }
    const FileDiff &file = details_->files.at(selected.first()->data(0, fileIndexRole).toInt());
    diffTitle_->setText(changeLabel(file.change));
    diff_->setFiles({{file.change.path, changeStatusText(file.change), FilePatch::parse(file.patch)}});
    showPane(splitter_, diffShare_);
}

// Shows a splitter's second pane, with the share it had last time.
void HistoryView::showPane(QSplitter *splitter, double &share)
{
    QWidget *pane = splitter->widget(1);
    if (!pane->isHidden())
        return;
    pane->show();
    const int total = splitter->orientation() == Qt::Horizontal ? splitter->width() : splitter->height();
    const int second = qRound(total * share);
    splitter->setSizes({total - second, second});
}

void HistoryView::hidePanes()
{
    commitPane_->hide();
    diffPane_->hide();
}

// Every patch of a large commit: read off the UI thread.
void HistoryView::showCommit(const QString &id)
{
    if ((details_ && details_->summary.id == id) || detailsWanted_ == id)
        return;
    detailsWanted_ = id;
    diffPane_->hide(); // the diff of another commit
    pool_.start([this, folder = folder_, id] {
        std::optional<CommitDetails> details;
        QString error;
        try {
            details = CommitDetails::read(folder, id);
        } catch (const GitError &exception) {
            error = QString::fromStdString(exception.what());
        }
        QMetaObject::invokeMethod(
            this, [this, id, details, error] { onDetailsRead(id, details, error); }, Qt::QueuedConnection);
    });
}

void HistoryView::onDetailsRead(const QString &id, const std::optional<CommitDetails> &details, const QString &error)
{
    if (id != detailsWanted_)
        return; // another commit was chosen meanwhile
    detailsWanted_.clear();
    details_ = details;
    showDetails();
    if (!details)
        message_->setPlainText(error);
}

void HistoryView::showDetails()
{
    files_->clear();
    diffPane_->hide(); // until a file of this commit is chosen
    if (!details_) {
        message_->clear();
        diff_->showMessage({});
        return;
    }

    const CommitDetails &details = *details_;
    const CommitSummary &commit = details.summary;
    commitTitle_->setText(tr("Commit %1").arg(commit.shortId));
    // The author's avatar, then the summary, the body, and the details.
    const QString muted = palette().color(QPalette::PlaceholderText).name();
    const qreal ratio = devicePixelRatioF();
    message_->document()->addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("avatar://author")),
                                      CommitDelegate::avatar(commit.author, 32, ratio).toImage());
    const QString body = details.message.section(QLatin1Char('\n'), 1).trimmed();
    QString html = QStringLiteral("<table cellspacing='0' cellpadding='0'><tr>"
                                  "<td style='padding-right:10px'><img src='avatar://author' width='32' height='32'></td>"
                                  "<td><b>%1</b><br><span style='color:%2'>%3 · %4</span></td></tr></table>")
                       .arg(commit.author.toHtmlEscaped(), muted, CommitDelegate::relativeDate(commit.date),
                            formatDate(commit.date));
    html += QStringLiteral("<p style='font-size:large; font-weight:600; margin-top:14px'>%1</p>").arg(commit.summary.toHtmlEscaped());
    if (!body.isEmpty())
        html += QStringLiteral("<p style='white-space:pre-wrap; margin-top:6px'>%1</p>").arg(body.toHtmlEscaped());
    html += QStringLiteral("<p style='color:%1; margin-top:12px'>").arg(muted);
    html += QStringLiteral("<span style='font-family:monospace'>%1</span> · %2")
                .arg(commit.id.left(10), commit.email.toHtmlEscaped());
    if (!details.committer.isEmpty())
        html += QStringLiteral("<br>") + tr("Committed by %1 on %2").arg(details.committer.toHtmlEscaped(), formatDate(details.commitDate));
    if (details.parentIds.size() > 1)
        html += QStringLiteral("<br>")
                + tr("Merge of %1 — diff against %2").arg(details.parentIds.join(QStringLiteral(", ")), details.parentIds.first());
    html += QStringLiteral("</p>");
    if (!commit.refs.isEmpty()) {
        html += QStringLiteral("<p>");
        for (const QString &ref : commit.refs)
            html += QStringLiteral("<span style='color:%1; font-weight:600'>%2</span>&nbsp;&nbsp; ")
                        .arg(CommitDelegate::refColor(ref).lighter(125).name(), ref.toHtmlEscaped());
        html += QStringLiteral("</p>");
    }

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
    html += QStringLiteral("<p style='margin-top:12px'><span style='color:%1'>%2</span>&nbsp;&nbsp; "
                           "<span style='color:%3; font-weight:600'>+%4</span>&nbsp; "
                           "<span style='color:%5; font-weight:600'>−%6</span></p>")
                .arg(muted,
                     details.files.size() == 1 ? tr("1 file changed") : tr("%1 files changed").arg(details.files.size()),
                     dark ? QStringLiteral("#3fb950") : QStringLiteral("#1a7f37"), QString::number(added),
                     dark ? QStringLiteral("#f85149") : QStringLiteral("#cf222e"), QString::number(removed));
    message_->setHtml(html);
    // Once the pane is laid out.
    QTimer::singleShot(0, this, &HistoryView::fitMessage);

    for (int i = 0; i < details.files.size(); ++i) {
        const FileDiff &file = details.files[i];
        auto *item = new QTreeWidgetItem(files_);
        FileChangeDelegate::setChange(item, file.change);
        FileChangeDelegate::setStats(item, patches[i]);
        item->setData(0, fileIndexRole, i);
    }
}

// The message as tall as its text (within reason), the files below it.
void HistoryView::fitMessage()
{
    const int total = panel_->height();
    if (total <= 0 || commitPane_->isHidden())
        return;
    message_->document()->setTextWidth(message_->viewport()->width());
    const int wanted = qCeil(message_->document()->size().height()) + 2 * message_->frameWidth() + 2;
    const int filesMinimum = std::min(120, total / 3);
    const int shown = std::clamp(wanted, 60, std::max(60, total - filesMinimum));
    panel_->setSizes({shown, std::max(total - shown, 0)});
}

void HistoryView::showCommitMenu(const QPoint &position)
{
    QTreeWidgetItem *item = commits_->itemAt(position);
    if (!item || !log_)
        return;
    const CommitSummary &commit = log_->commits.at(commits_->indexOfTopLevelItem(item));
    QMenu menu;
    menu.addAction(tr("Copy ID"), this, [id = commit.id] { QApplication::clipboard()->setText(id); });
    menu.addAction(tr("Copy Short ID"), this,
                   [id = commit.shortId] { QApplication::clipboard()->setText(id); });
    menu.addAction(tr("Copy Summary"), this,
                   [summary = commit.summary] { QApplication::clipboard()->setText(summary); });
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
    refreshButton_->setIcon(UiIcons::icon(QStringLiteral("refresh"), theme.foreground));
    searchIcon_->setIcon(UiIcons::icon(QStringLiteral("search"), theme.mutedText));
}

QByteArray HistoryView::saveState() const
{
    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream << panelShare_ << diffShare_ << panel_->saveState();
    return state;
}

void HistoryView::restoreState(const QByteArray &state)
{
    QDataStream stream(state);
    double panelShare = 0;
    double diffShare = 0;
    QByteArray panel;
    stream >> panelShare >> diffShare >> panel;
    // Also rejects the old format (raw QSplitter state).
    if (stream.status() != QDataStream::Ok || panelShare <= 0 || panelShare >= 1 || diffShare <= 0 || diffShare >= 1)
        return;
    panelShare_ = panelShare;
    diffShare_ = diffShare;
    panel_->restoreState(panel);
}
