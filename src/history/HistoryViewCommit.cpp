// SPDX-License-Identifier: GPL-3.0-only

// HistoryView: the reading of the chosen commit's details, and the diff of
// its files.

#include "HistoryView.h"
#include "changes/DiffFileStrip.h"
#include "changes/DiffHeaderBar.h"
#include "changes/DiffView.h"
#include "git/ChangeKindStyle.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace {

// Whether `change` is the file or under the folder `filter` (an empty
// filter: every file), by its path or the one it was renamed from.
bool isFiltered(const FileChange &change, const QString &filter)
{
    const auto isUnder = [&filter](const QString &path) {
        return path == filter || path.startsWith(filter + QLatin1Char('/'));
    };
    return filter.isEmpty() || isUnder(change.path) || (!change.oldPath.isEmpty() && isUnder(change.oldPath));
}

} // namespace

// Only the filtered file or folder: no need to look for it.
void HistoryView::showCommitFiles()
{
    if (!details_) {
        diffHeader_->setPlaceholder(diffPlaceholder());
        diff_->showMessage({});
        return;
    }
    const QString filter = filterOf(details_->summary.id);
    QList<DiffFile> shown;
    for (const FileDiff &file : details_->files) {
        if (isFiltered(file.change, filter))
            shown.append({file.change.path, changeStatusText(file.change), FilePatch::parse(file.patch),
                          file.isTooLarge, file.newText});
    }
    diff_->setFiles(shown);
}

void HistoryView::openFile(const QString &relativePath)
{
    if (QFileInfo(QDir(folder_).filePath(relativePath)).isFile())
        emit openFileRequested(relativePath);
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
    const QList<DiffView::FileSummary> &files = diff_->fileSummaries();
    const auto file = std::ranges::find_if(files, [&path](const DiffView::FileSummary &each) { return each.path == path; });
    if (file != files.end())
        diff_->scrollToTop(diff_->fileStart(int(file - files.begin())));
}

// What to choose for a diff to show: a commit.
QString HistoryView::diffPlaceholder() const
{
    return filtersOnFile_ ? tr("Choose a commit to see its changes to %1").arg(pathFilter_)
                          : tr("Choose a commit to see its message and changes");
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
    const QList<DiffView::FileSummary> &files = diff_->fileSummaries();
    if (const int current = fileStrip_->currentFile(); current >= 0 && current < files.size())
        fileToKeep_ = files.at(current).path;
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
    if (!details)
        diffHeader_->setPlaceholder(error); // why the commit cannot be read
}

void HistoryView::showDetails()
{
    diff_->hideUnchangedLines(); // those of the previous commit
    showCommitFiles();
    if (!fileToKeep_.isEmpty())
        chooseFile(std::exchange(fileToKeep_, {}));
}
