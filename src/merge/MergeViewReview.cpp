// SPDX-License-Identifier: GPL-3.0-only

#include "MergeReadyPanel.h"
#include "MergeView.h"
#include "changes/DiffView.h"
#include "git/GitCommands.h"

#include <QPushButton>
#include <QStackedWidget>

void MergeView::showReady()
{
    QString message = conflicts_.message;
    if (message.isEmpty())
        message = tr("Merge branch '%1' into %2").arg(conflicts_.sides.incoming, conflicts_.sides.current);
    ready_->showSummary(conflicts_.repository.operation, mergedPaths_, message);
    QList<DiffFile> files;
    for (const FileDiff &diff : conflicts_.staged) {
        files.append({diff.change.path, tr("Staged"), FilePatch::parse(diff.patch), diff.isTooLarge, diff.newText,
                      diff.conflict});
    }
    review_->setFiles(files);
    pages_->setCurrentWidget(ready_);
    reviewFiles_->setText(tr("Review Files"));
}

void MergeView::toggleReview()
{
    const bool review = pages_->currentWidget() != review_;
    pages_->setCurrentWidget(review ? static_cast<QWidget *>(review_) : ready_);
    reviewFiles_->setText(review ? tr("Back to Summary") : tr("Review Files"));
}

std::optional<QString> MergeView::mergeMessage() const
{
    if (conflicts_.repository.operation == tr("merge"))
        return ready_->message();
    return std::nullopt;
}

void MergeView::setFinalizing(bool finalizing)
{
    finalizing_ = finalizing;
    if (finalizing)
        pages_->setCurrentWidget(ready_);
    ready_->setFinalizing(finalizing);
    working_->setEnabled(!finalizing && !commands_->isBusy());
    updateFooter();
}
