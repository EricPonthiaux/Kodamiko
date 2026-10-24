// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QString>

class QProcess;

// The project's check (ProjectConfig::agentCheckCommand: a build, the tests,
// a linter…), run by the system's shell in a folder: before an agent's
// commits are merged (AgentMergeQueue), before a review (ReviewView). One
// run at a time; start() again stops the previous one.
class ProjectCheck : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ProjectCheck)

public:
    struct Result {
        // -1 when the shell did not start or the check crashed.
        int exitCode = -1;
        QString output; // standard output and error, interleaved

        bool hasPassed() const { return exitCode == 0; }

        // The last `count` lines of the output: where a failure is told.
        QString lastLines(int count) const;
    };

    using QObject::QObject;
    ~ProjectCheck() override;

    void start(const QString &command, const QString &folder);
    // Kills the check; no signal follows.
    void stop();

signals:
    void finished(const ProjectCheck::Result &result);

private:
    void finish(int exitCode);

    QProcess *process_ = nullptr; // the running check, a child
};
