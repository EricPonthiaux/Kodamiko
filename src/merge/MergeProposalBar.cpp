// SPDX-License-Identifier: GPL-3.0-only

#include "MergeProposalBar.h"

#include "ui/AppFonts.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int marginPx = 10;
constexpr int spacingPx = 8;

} // namespace

MergeProposalBar::MergeProposalBar(QWidget *parent)
    : QFrame(parent)
    , title_(new QLabel(this))
    , reason_(new QLabel(this))
    , previous_(new QPushButton(tr("Previous"), this))
    , next_(new QPushButton(tr("Next"), this))
    , acceptAll_(new QPushButton(this))
{
    setObjectName(QStringLiteral("mergeProposalBar"));
    AppFonts::setInterfaceFont(title_, 1.0, QFont::DemiBold);
    title_->setTextFormat(Qt::PlainText);
    reason_->setTextFormat(Qt::PlainText);
    reason_->setWordWrap(true);
    auto *accept = new QPushButton(tr("Accept"), this);
    accept->setDefault(true);
    auto *reject = new QPushButton(tr("Reject"), this);
    connect(accept, &QPushButton::clicked, this, &MergeProposalBar::acceptRequested);
    connect(reject, &QPushButton::clicked, this, &MergeProposalBar::rejectRequested);
    connect(acceptAll_, &QPushButton::clicked, this, &MergeProposalBar::acceptAllRequested);
    connect(previous_, &QPushButton::clicked, this, &MergeProposalBar::previousRequested);
    connect(next_, &QPushButton::clicked, this, &MergeProposalBar::nextRequested);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(spacingPx);
    buttons->addWidget(accept);
    buttons->addWidget(reject);
    buttons->addStretch();
    buttons->addWidget(previous_);
    buttons->addWidget(next_);
    buttons->addWidget(acceptAll_);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(marginPx, marginPx, marginPx, marginPx);
    layout->setSpacing(spacingPx);
    layout->addWidget(title_);
    layout->addWidget(reason_);
    layout->addLayout(buttons);
}

void MergeProposalBar::showProposal(int index, int count, const QString &agentName, const QString &reason)
{
    title_->setText(tr("%1's proposal %2 of %3").arg(agentName).arg(index + 1).arg(count));
    reason_->setText(reason);
    reason_->setVisible(!reason.isEmpty());
    previous_->setVisible(count > 1);
    next_->setVisible(count > 1);
    acceptAll_->setText(tr("Accept All %1").arg(count));
    acceptAll_->setVisible(count > 1);
}
