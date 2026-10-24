// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "terminal/ProcessSpec.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <chrono>

class AgentProvider;
class QProcess;
class QTimer;

// A run of the agent without a session, outside the terminal (a file run,
// a review run): its process, what the agent does as it goes, and its
// answer. One run at a time; start() again stops the previous one.
class AgentRun : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AgentRun)

public:
    // How a run that went to its end ended.
    struct Outcome {
        QString answer;      // the agent's final answer, or its error
        bool failed = false; // the agent reported a failure
        QString errorOutput; // the process's standard error
    };

    // `agent` decodes the output; the steps show paths relative to
    // `workDir`. `agent` is not owned and outlives the run.
    AgentRun(const AgentProvider *agent, QString workDir, QObject *parent);
    ~AgentRun() override;

    // Runs `spec`, killed after `timeout`.
    void start(const ProcessSpec &spec, std::chrono::milliseconds timeout);
    // Kills the process; no signal follows.
    void stop();

signals:
    // What the agent does now ("Reading src/a.cpp").
    void stepsAdded(const QStringList &steps);
    // The process exited.
    void finished(const AgentRun::Outcome &outcome);
    // The process could not start, or ran out of time.
    void failed(const QString &error);

private:
    void readOutput();
    void onProcessFinished();

    const AgentProvider *agent_;
    QString workDir_;
    QProcess *process_ = nullptr;
    QTimer *timeout_;
    QByteArray pending_; // output not yet split into lines
    Outcome outcome_;
};
