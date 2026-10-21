// SPDX-License-Identifier: GPL-3.0-only

// HistoryView: the panes' arrangement for the view's width, their
// proportions, and the commits and the commit folded away.

#include "HistoryView.h"
#include "changes/DiffHeaderBar.h"
#include "ui/FoldedPane.h"
#include "ui/GripSplitter.h"
#include "ui/UiMetrics.h"
#include "ui/ViewHeader.h"

#include <QDataStream>
#include <QLabel>
#include <QTreeWidget>

#include <numeric>

namespace {

// From this width, the three panes side by side.
constexpr int wideLayoutWidth = 1180;

} // namespace

void HistoryView::setCommitListFolded(bool folded)
{
    isCommitListFolded_ = folded;
    showPanes();
}

void HistoryView::setCommitPaneFolded(bool folded)
{
    isCommitPaneFolded_ = folded;
    showPanes();
    onFileSelectionChanged(); // the diff of the chosen file, or of them all
}

bool HistoryView::showsCommitPane() const
{
    return !isCommitPaneFolded_ && !filtersOnFile_;
}

void HistoryView::showPanes()
{
    commitListPane_->setVisible(!isCommitListFolded_);
    commitPane_->setVisible(showsCommitPane());
    foldedCommits_->setVisible(isCommitListFolded_);
    foldedCommit_->setVisible(isCommitPaneFolded_ && !filtersOnFile_);
    // Emptied, the left splitter would still take its share of the width.
    const bool hasLeftPane = !isCommitListFolded_ || (!wide_ && showsCommitPane());
    leftSplitter_->setVisible(hasLeftPane);
    applyShares();
    showCurrentCommitTitle();
}

void HistoryView::showCurrentCommitTitle()
{
    const QList<QTreeWidgetItem *> selected = commits_->selectedItems();
    const bool isShown = isCommitListFolded_ && log_ && !selected.isEmpty();
    currentCommit_->setVisible(isShown);
    if (!isShown)
        return;
    const CommitSummary &commit = log_->commits.at(commits_->indexOfTopLevelItem(selected.first()));
    currentCommit_->setText(commit.summary);
    currentCommit_->setToolTip(QStringLiteral("%1  %2").arg(commit.shortId, commit.summary));
}

void HistoryView::alignBars()
{
    // As high as a side panel's header, unless the diff's controls need more.
    const int height = std::max(UiMetrics::scaled(UiMetrics::panelHeaderHeight), diffHeader_->controlsHeight());
    commitsHeader_->setFixedHeight(height);
    commitHeader_->setFixedHeight(height);
    foldedCommits_->setButtonRowHeight(height);
    foldedCommit_->setButtonRowHeight(height);
    // Its controls hidden, the diff's header would be as high as its hint.
    diffHeader_->setMinimumHeight(height);
    // The hint can wrap as the pane narrows: its rule follows the header,
    // and meets the bar's at the left while they are as high.
    const int diffHeight = diffHeader_->height();
    const Qt::Edges sides = diffHeight == height ? Qt::LeftEdge | Qt::RightEdge : Qt::RightEdge;
    splitter_->setHandleRule(splitter_->indexOf(diffHeader_->parentWidget()), {diffHeight - 1, sides, ruleColor_});
    if (wide_)
        splitter_->setHandleRule(splitter_->indexOf(commitPane_),
                                 {height - 1, Qt::LeftEdge | Qt::RightEdge, ruleColor_});
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
    showPanes();
    alignBars();
    arranging_ = false;
}

void HistoryView::applyShares()
{
    // The panes' widths with none folded: a folded (or hidden) pane leaves
    // its width to the diff, the others keep theirs.
    const int whole = width();
    if (whole <= 0)
        return;
    const int rest = whole - qRound(whole * diffShare_);
    const int commit = qRound(rest * commitShare_);
    // The splitter's width, once the folded commits' bar is beside it.
    const int total = whole - (isCommitListFolded_ ? foldedCommits_->sizeHint().width() : 0);
    if (wide_) {
        const int commitsWidth = isCommitListFolded_ ? 0 : rest - commit;
        const int commitWidth = showsCommitPane() ? commit : 0;
        splitter_->setSizes({commitsWidth, commitWidth, total - commitsWidth - commitWidth});
        return;
    }
    const int leftWidth = leftSplitter_->isHidden() ? 0 : rest;
    splitter_->setSizes({leftWidth, total - leftWidth});
    const int height = leftSplitter_->height();
    const int commitHeight = qRound(height * commitShare_);
    leftSplitter_->setSizes({height - commitHeight, commitHeight});
}

void HistoryView::rememberShares()
{
    // A folded pane has no share to tell.
    const bool isAnyFolded = isCommitListFolded_ || isCommitPaneFolded_;
    if (arranging_ || isAnyFolded)
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
    stream << commitShare_ << diffShare_ << QByteArray() << isCommitListFolded_ << isCommitPaneFolded_;
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
    // The folds came later: a state saved before has none.
    bool isCommitListFolded = false;
    bool isCommitPaneFolded = false;
    stream >> isCommitListFolded >> isCommitPaneFolded;
    if (stream.status() == QDataStream::Ok) {
        isCommitListFolded_ = isCommitListFolded;
        isCommitPaneFolded_ = isCommitPaneFolded;
    }
    showPanes();
    onFileSelectionChanged();
}
