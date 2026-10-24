// SPDX-License-Identifier: GPL-3.0-only

#include "AgentRun.h"

#include "AgentProvider.h"

#include <QProcess>
#include <QTimer>

#include <utility>

namespace {

constexpr int killWaitMs = 1000;

} // namespace

AgentRun::AgentRun(const AgentProvider *agent, QString workDir, QObject *parent)
    : QObject(parent)
    , agent_(agent)
    , workDir_(std::move(workDir))
    , timeout_(new QTimer(this))
{
    Q_ASSERT(agent_);
    timeout_->setSingleShot(true);
    connect(timeout_, &QTimer::timeout, this, [this] {
        stop();
        emit failed(tr("%1 did not finish in time.").arg(agent_->name()));
    });
}

AgentRun::~AgentRun()
{
    stop();
}

void AgentRun::start(const ProcessSpec &spec, std::chrono::milliseconds timeout)
{
    stop();
    pending_.clear();
    outcome_ = {};

    process_ = new QProcess(this);
    process_->setProgram(spec.program);
    process_->setArguments(spec.arguments);
    process_->setWorkingDirectory(spec.workingDirectory);
    process_->setProcessEnvironment(spec.environment);
    connect(process_, &QProcess::readyReadStandardOutput, this, &AgentRun::readOutput);
    connect(process_, &QProcess::finished, this, &AgentRun::onProcessFinished);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart)
            return;
        const QString error = tr("Could not start %1: %2").arg(agent_->name(), process_->errorString());
        stop();
        emit failed(error);
    });
    process_->start();
    process_->closeWriteChannel();
    timeout_->start(timeout);
}

void AgentRun::stop()
{
    timeout_->stop();
    if (!process_)
        return;
    process_->disconnect(this);
    process_->kill();
    process_->waitForFinished(killWaitMs);
    process_->deleteLater();
    process_ = nullptr;
}

void AgentRun::readOutput()
{
    pending_ += process_->readAllStandardOutput();
    qsizetype newline = 0;
    while ((newline = pending_.indexOf('\n')) >= 0) {
        const QByteArray line = pending_.left(newline);
        pending_.remove(0, newline + 1);
        const AgentRunEvent event = agent_->parseRunOutput(line, workDir_);
        switch (event.kind) {
        case AgentRunEvent::Kind::None:
            break;
        case AgentRunEvent::Kind::Steps:
            emit stepsAdded(event.steps);
            break;
        case AgentRunEvent::Kind::Finished:
            outcome_.answer = event.text;
            outcome_.failed = event.failed;
            break;
        }
    }
}

void AgentRun::onProcessFinished()
{
    readOutput();
    outcome_.errorOutput = QString::fromUtf8(process_->readAllStandardError()).trimmed();
    stop();
    emit finished(outcome_);
}
