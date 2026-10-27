// SPDX-License-Identifier: GPL-3.0-only

// HistoryView: the panes' proportions, and the commits folded away.

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

// The saved state's version, after the folds: none before the commit's pane
// became the diff's header.
constexpr int twoPaneVersion = 2;
// Written where the commit's pane once saved its share: read back as one.
constexpr double formerCommitShare = 0.5;

} // namespace

void HistoryView::setCommitListFolded(bool folded)
{
    isCommitListFolded_ = folded;
    showPanes();
}

void HistoryView::showPanes()
{
    commitListPane_->setVisible(!isCommitListFolded_);
    foldedCommits_->setVisible(isCommitListFolded_);
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
    foldedCommits_->setButtonRowHeight(height);
    // Its controls hidden, the diff's header would be as high as its hint.
    diffHeader_->setMinimumHeight(height);
    // The rules under the two headers meet across the handle while they are
    // as high (the diff's hint can wrap).
    const bool meets = diffHeader_->height() == height;
    splitter_->setHandleRule(1, {height - 1, meets ? Qt::LeftEdge | Qt::RightEdge : Qt::LeftEdge, ruleColor_});
}

void HistoryView::applyShares()
{
    // Folded, the commits leave their width to the diff.
    const int whole = width();
    if (whole <= 0)
        return;
    const int total = whole - (isCommitListFolded_ ? foldedCommits_->sizeHint().width() : 0);
    const int commitsWidth = isCommitListFolded_ ? 0 : whole - qRound(whole * diffShare_);
    splitter_->setSizes({commitsWidth, total - commitsWidth});
}

void HistoryView::rememberShares()
{
    // Folded, the commits have no share to tell.
    if (isCommitListFolded_)
        return;
    const QList<int> sizes = splitter_->sizes();
    const int total = std::accumulate(sizes.begin(), sizes.end(), 0);
    if (total <= 0 || sizes.last() <= 0)
        return;
    diffShare_ = double(sizes.last()) / total;
}

QByteArray HistoryView::saveState() const
{
    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    // The empty array once held a splitter's state, the share and the fold
    // after it the commit's pane's: the format stays readable both ways.
    stream << formerCommitShare << diffShare_ << QByteArray() << isCommitListFolded_ << false << twoPaneVersion;
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
    // The folds came later, the version after them: a state saved before has none.
    bool isCommitListFolded = false;
    bool wasCommitPaneFolded = false;
    int version = 0;
    stream >> isCommitListFolded >> wasCommitPaneFolded;
    if (stream.status() == QDataStream::Ok)
        isCommitListFolded_ = isCommitListFolded;
    stream >> version;
    // Saved with three panes: the diff takes the commit's pane's width too.
    const bool isThreePaneState = stream.status() != QDataStream::Ok || version < twoPaneVersion;
    diffShare_ = isThreePaneState ? 1 - (1 - diffShare) * (1 - commitShare) : diffShare;
    showPanes();
}
