// SPDX-License-Identifier: GPL-3.0-only

// MergeTextPanel: the agent's proposals, in the result and in the bar under
// the columns.

#include "MergeProposalBar.h"
#include "MergeTextPanel.h"

#include <algorithm>

void MergeTextPanel::showProposals()
{
    refresh();
    const QList<int> proposals = proposalSegments();
    if (proposals.isEmpty())
        return;
    proposalSegment_ = proposals.first();
    updateProposalBar();
    showConflict(proposalSegment_);
}

void MergeTextPanel::connectProposalBar()
{
    connect(proposalBar_, &MergeProposalBar::acceptRequested, this, [this] {
        const int accepted = proposalSegment_;
        document_.acceptProposal(accepted);
        refresh();
        moveOnFrom(accepted);
    });
    connect(proposalBar_, &MergeProposalBar::rejectRequested, this, [this] {
        document_.rejectProposal(proposalSegment_);
        refresh();
    });
    connect(proposalBar_, &MergeProposalBar::acceptAllRequested, this, [this] {
        document_.acceptAllProposals();
        refresh();
    });
    connect(proposalBar_, &MergeProposalBar::previousRequested, this, [this] { goToProposal(Direction::Previous); });
    connect(proposalBar_, &MergeProposalBar::nextRequested, this, [this] { goToProposal(Direction::Next); });
}

void MergeTextPanel::goToProposal(Direction direction)
{
    const QList<int> proposals = proposalSegments();
    if (proposals.isEmpty())
        return;
    const qsizetype step = direction == Direction::Next ? 1 : proposals.size() - 1;
    proposalSegment_ = proposals[(proposals.indexOf(proposalSegment_) + step) % proposals.size()];
    updateProposalBar();
    showConflict(proposalSegment_);
}

void MergeTextPanel::updateProposalBar()
{
    const QList<int> proposals = proposalSegments();
    proposalBar_->setVisible(!proposals.isEmpty());
    if (proposals.isEmpty())
        return;
    // After the one taken or rejected: the next one.
    if (!proposals.contains(proposalSegment_)) {
        const auto next = std::ranges::find_if(proposals, [this](int i) { return i > proposalSegment_; });
        proposalSegment_ = next == proposals.end() ? proposals.first() : *next;
    }
    proposalBar_->showProposal(int(proposals.indexOf(proposalSegment_)), int(proposals.size()), agentName_,
                               document_.proposalFor(proposalSegment_)->reason);
}

QList<int> MergeTextPanel::proposalSegments() const
{
    QList<int> proposals;
    for (int i = 0; i < document_.segments().size(); ++i) {
        if (document_.proposalFor(i))
            proposals.append(i);
    }
    return proposals;
}
