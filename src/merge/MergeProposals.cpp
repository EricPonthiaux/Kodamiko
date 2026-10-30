// SPDX-License-Identifier: GPL-3.0-only

#include "MergeProposals.h"

#include "agent/AgentProvider.h"

#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>

#include <chrono>

namespace {

// The agent may read a good part of the project.
constexpr std::chrono::minutes timeout{10};

} // namespace

MergeProposals::MergeProposals(QObject *parent)
    : QObject(parent)
{
}

MergeProposals::~MergeProposals() = default;

void MergeProposals::start(const Ask &ask, const MergeDocument &document)
{
    stop();
    const MergeRequest::Conflicts conflicts = MergeRequest::conflicts(document, ask.sides);
    copyFolder_ = std::make_unique<QTemporaryDir>();
    // The copy keeps the file's name, for the agent to know its language.
    QSaveFile copy(copyFolder_->filePath(QFileInfo(ask.path).fileName()));
    const bool isWritten = copyFolder_->isValid() && copy.open(QIODevice::WriteOnly) &&
                           copy.write(conflicts.text) == conflicts.text.size() && copy.commit();
    if (!isWritten) {
        fail(tr("Could not write the conflicts of %1 in %2: %3").arg(ask.path, copy.fileName(), copy.errorString()));
        return;
    }
    QString launchError;
    const std::optional<ProcessSpec> spec =
        ask.agent->reviewRunProcess({.workingDirectory = ask.workDir,
                                     .prompt = MergeRequest::prompt({.path = ask.path,
                                                                     .title = ask.title,
                                                                     .sides = ask.sides,
                                                                     .copyPath = copy.fileName(),
                                                                     .conflictCount = conflicts.chunks.size()}),
                                     .answerSchema = MergeRequest::schema(),
                                     .model = {}},
                                    &launchError);
    if (!spec) {
        fail(launchError);
        return;
    }
    path_ = ask.path;
    agentName_ = ask.agent->name();
    conflicts_ = conflicts.chunks;
    run_ = new AgentRun(ask.agent, ask.workDir, this);
    connect(run_, &AgentRun::stepsAdded, this, &MergeProposals::stepsAdded);
    connect(run_, &AgentRun::finished, this, &MergeProposals::finish);
    connect(run_, &AgentRun::failed, this, &MergeProposals::fail);
    run_->start(*spec, timeout);
}

void MergeProposals::stop()
{
    if (!run_)
        return;
    run_->stop();
    run_->deleteLater();
    run_ = nullptr;
}

void MergeProposals::finish(const AgentRun::Outcome &outcome)
{
    stop();
    if (outcome.failed || outcome.answer.isEmpty()) {
        const QString error = outcome.answer.isEmpty() ? outcome.errorOutput : outcome.answer;
        emit failed(error.isEmpty() ? tr("%1 gave no answer.").arg(agentName_) : error);
        return;
    }
    const std::optional<MergeRequest::Proposals> proposals = MergeRequest::parse(outcome.answer, conflicts_);
    if (!proposals) {
        emit failed(tr("%1's answer could not be read:\n%2").arg(agentName_, outcome.answer));
        return;
    }
    emit proposed(path_, *proposals);
}

void MergeProposals::fail(const QString &error)
{
    stop();
    emit failed(error);
}
