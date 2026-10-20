// HistoryView: the chosen commit's pane (its message, counts and files), the
// reading of its details, and the diff of its chosen file.

#include "CommitDelegate.h"
#include "HistoryView.h"
#include "changes/ChangeTree.h"
#include "changes/DiffHeaderBar.h"
#include "changes/DiffView.h"
#include "changes/FileChangeDelegate.h"
#include "git/ChangeKindStyle.h"
#include "ui/AppFonts.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>
#include <utility>

namespace {

constexpr int fileIndexRole = Qt::UserRole; // on a file row, not on a folder row
constexpr int treeIndentation = 14;
// The message shows this many lines until expanded.
constexpr int collapsedMessageLines = 5;
constexpr int avatarSize = 18;

QString formatDate(const QDateTime &date)
{
    return QLocale().toString(date.toLocalTime(), QLocale::ShortFormat);
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
    files_->setIndentation(treeIndentation);
    files_->setUniformRowHeights(true);
    files_->setFrameShape(QFrame::NoFrame);
    files_->setMouseTracking(true);
    files_->setItemDelegate(new FileChangeDelegate(files_));
    files_->setToolTip(tr("Double-click: open the file in the editor"));
    connect(files_, &QTreeWidget::itemSelectionChanged, this, &HistoryView::onFileSelectionChanged);
    connect(files_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const QVariant index = item->data(0, fileIndexRole);
        if (!details_ || !index.isValid())
            return; // a folder: opened or closed
        const FileChange &change = details_->files.at(index.toInt()).change;
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

void HistoryView::onFileSelectionChanged()
{
    const QList<QTreeWidgetItem *> selected = files_->selectedItems();
    if (selected.isEmpty() || !details_) {
        diffHeader_->setPlaceholder(diffPlaceholder());
        diff_->showMessage({});
        return;
    }
    // A folder: the diffs of all its files.
    QList<DiffFile> shown;
    for (const QTreeWidgetItem *row : ChangeTree::filesUnder(selected.first())) {
        const FileDiff &file = details_->files.at(row->data(0, fileIndexRole).toInt());
        shown.append({file.change.path, changeStatusText(file.change), FilePatch::parse(file.patch), file.isTooLarge,
                      file.newText});
    }
    diff_->setFiles(shown);
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
    for (QTreeWidgetItem *row : ChangeTree::filesUnder(files_->invisibleRootItem())) {
        if (details_->files.at(row->data(0, fileIndexRole).toInt()).change.path == path) {
            files_->setCurrentItem(row);
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
    const QTreeWidgetItem *item = files_->currentItem();
    if (item && details_ && item->data(0, fileIndexRole).isValid())
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
    QList<int> kept;
    QStringList paths;
    for (int i = 0; i < details.files.size(); ++i) {
        if (isFiltered(details.files[i].change, filter)) {
            kept.append(i);
            paths.append(details.files[i].change.path);
        }
    }
    const QList<QTreeWidgetItem *> rows = ChangeTree::addFiles(files_->invisibleRootItem(), paths);
    for (qsizetype k = 0; k < kept.size(); ++k) {
        FileChangeDelegate::setChange(rows[k], details.files[kept[k]].change);
        FileChangeDelegate::setStats(rows[k], patches[kept[k]]);
        rows[k]->setData(0, fileIndexRole, kept[k]);
    }
    if (!pathFilter_.isEmpty() && rows.size() == 1)
        files_->setCurrentItem(rows.first()); // the filtered file's diff right away
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
