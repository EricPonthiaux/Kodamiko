// SPDX-License-Identifier: GPL-3.0-only

// MergeView: the agent's proposals for the open conflicts of the file shown.

#include "MergeProposals.h"
#include "MergeTextPanel.h"
#include "MergeTexts.h"
#include "MergeView.h"
#include "agent/AgentProvider.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>

void MergeView::setAgent(const AgentProvider *agent)
{
    if (agent != agent_)
        proposals_->stop();
    agent_ = agent;
    updateAgentButton();
}

void MergeView::connectProposals()
{
    connect(proposals_, &MergeProposals::stepsAdded, this, [this](const QStringList &steps) {
        if (!steps.isEmpty())
            hint_->setText(tr("%1: %2").arg(agent_ ? agent_->name() : QString(), steps.last()));
    });
    connect(proposals_, &MergeProposals::proposed, this, &MergeView::showProposals);
    connect(proposals_, &MergeProposals::failed, this, [this](const QString &error) {
        hint_->setText(error);
        updateAgentButton();
    });
}

void MergeView::askOrStopAgent()
{
    if (proposals_->isRunning()) {
        proposals_->stop();
        hint_->setText(tr("%1 was stopped.").arg(agent_->name()));
        updateAgentButton();
        return;
    }
    const ConflictVersions::File *file = currentFile();
    const MergeDocument *document = file ? openedDocument(file->path) : nullptr;
    if (!agent_ || !document)
        return;
    hint_->setText(tr("%1 is reading the conflicts of %2…").arg(agent_->name(), file->path));
    proposals_->start({.agent = agent_,
                       .workDir = conflicts_.repository.workDir,
                       .path = file->path,
                       .title = MergeTexts::title(conflicts_.repository.operation, conflicts_.sides),
                       .sides = conflicts_.sides},
                      *document);
    updateAgentButton();
}

void MergeView::showProposals(const QString &path, const MergeRequest::Proposals &proposals)
{
    updateAgentButton();
    const auto found = resolutions_.find(path);
    // The file was resolved, or git's versions changed, meanwhile.
    if (found == resolutions_.end())
        return;
    for (const auto &[conflict, proposal] : proposals)
        found->second.document->propose(conflict, proposal);
    const ConflictVersions::File *file = currentFile();
    if (panel_ && file && file->path == path)
        panel_->showProposals();
    hint_->setText(
        proposals.isEmpty()
            ? tr("%1 proposed nothing for %2: its conflicts need your decision.").arg(agent_->name(), path)
            : tr("%1 proposed %n resolution(s) for %2: accept or reject each.", nullptr, int(proposals.size()))
                  .arg(agent_->name(), path));
}

void MergeView::updateAgentButton()
{
    const QString name = agent_ ? agent_->name() : tr("the agent");
    if (proposals_->isRunning()) {
        askAgent_->setText(tr("Stop %1").arg(name));
        askAgent_->setEnabled(true);
        return;
    }
    const ConflictVersions::File *file = currentFile();
    const MergeDocument *document = file ? openedDocument(file->path) : nullptr;
    askAgent_->setText(tr("Ask %1").arg(name));
    askAgent_->setToolTip(tr("Have %1 propose a resolution for each open conflict of this file; nothing is "
                             "written until you accept")
                              .arg(name));
    askAgent_->setEnabled(agent_ && panel_ && document && !document->isResolved());
}
