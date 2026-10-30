// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "MergeRequest.h"
#include "agent/AgentRun.h"

#include <QObject>

#include <memory>

class AgentProvider;
class QTemporaryDir;

// Asks the agent, outside the terminal and from a blank conversation, for
// proposals on the open conflicts of one file (MergeRequest). The agent
// reads the project and changes nothing. One request at a time.
class MergeProposals : public QObject {
    Q_OBJECT

public:
    // What a request needs besides the file's document.
    struct Ask {
        const AgentProvider *agent = nullptr; // not owned, outlives the request
        QString workDir;                      // the repository's root
        QString path;                         // the file, a repository path
        QString title;                        // the operation, for the agent
        Integration::ConflictSides sides;
    };

    explicit MergeProposals(QObject *parent = nullptr);
    ~MergeProposals() override;

    // Asks for proposals on the open conflicts of `document`, stopping the
    // request in progress. failed() may be emitted before it returns.
    void start(const Ask &ask, const MergeDocument &document);
    // Gives up the request in progress: no signal follows.
    void stop();

    bool isRunning() const { return run_ != nullptr; }

signals:
    // What the agent does now ("Reading src/a.cpp").
    void stepsAdded(const QStringList &steps);
    // The agent's proposals for the file `path` (repository path); empty
    // when it resolved none.
    void proposed(const QString &path, const MergeRequest::Proposals &proposals);
    void failed(const QString &error);

private:
    void finish(const AgentRun::Outcome &outcome);
    void fail(const QString &error);

    AgentRun *run_ = nullptr;
    std::unique_ptr<QTemporaryDir> copyFolder_;
    QString path_;
    QString agentName_;
    QList<ThreeWayMerge::Chunk> conflicts_;
};
