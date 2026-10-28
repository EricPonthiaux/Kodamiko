// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "AgentRun.h"
#include "ReviewReport.h"
#include "ReviewRequest.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

#include <chrono>
#include <memory>
#include <optional>

class AgentProvider;
class ProblemStore;
class QTemporaryDir;

// A review run (AgentReviewRun) and what it found, for the views that show
// it (ReviewView, a tab; ReviewBar, over the diff): the project's check
// first, when it has one, then the reviewer, then its findings. A failing
// check stops there, as reviewing code that does not build costs much for
// little: reviewAnyway() goes on, the failure told to the reviewer. The
// reviewer is a separate agent process that starts from a blank
// conversation and may only read: it judges the code without the reasoning
// of the session that wrote it.
//
// The findings are also problems of the ProblemStore, underlined in the
// editor and listed in the Problems pane, until they are dismissed or the
// session is destroyed.
class ReviewSession : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ReviewSession)

public:
    // `agent` reviews `subject` in `workDir`.
    struct Request {
        QString workDir;
        QString subject; // what is reviewed, in words: "src/a.cpp, lines 2-3"
        // A unified diff of the changes to review; empty to review the code
        // as it is.
        QByteArray patch;
        QString checkCommand;                 // the project's check, run first; empty for none
        QString model;                        // as the agent names it; empty for its default
        const AgentProvider *agent = nullptr; // not owned, never null
        ProblemStore *problems = nullptr;     // not owned; null: findings in the views only
    };

    enum class State {
        Running, // the check or the reviewer
        CheckFailed,
        Finished, // a report, or an answer that is not one (rawAnswer())
        Failed
    };

    explicit ReviewSession(Request review, QObject *parent = nullptr);
    ~ReviewSession() override;

    // Runs the check, then the review; again, from the check.
    void start();
    // After a failing check: the review, told the failure.
    void reviewAnyway();
    // Stops the check or the reviewer; the state stays as it was.
    void stop();

    State state() const { return state_; }

    const QString &workDir() const { return workDir_; }

    const QString &subject() const { return subject_; }

    QString agentName() const;
    // The agent, and its model when one was chosen.
    QString reviewerName() const;
    // Time spent running since start(): the check, then the review; frozen
    // while nothing runs.
    std::chrono::milliseconds elapsed() const;

    // What the reviewer did, the latest last.
    const QStringList &steps() const { return steps_; }

    // CheckFailed: the check that failed.
    const std::optional<ReviewRequest::Check> &failedCheck() const { return failedCheck_; }

    // Failed: why.
    const QString &error() const { return error_; }

    // Finished: the reviewer's summary, empty when it gave none.
    const QString &summary() const { return summary_; }

    // Finished: the findings left, the most severe first.
    const QList<ReviewReport::Finding> &findings() const { return findings_; }

    // Finished: the answer, when it is not the JSON asked for.
    const QString &rawAnswer() const { return rawAnswer_; }

    // Leaves `findings` out of the findings and the ProblemStore.
    void dismiss(const QList<ReviewReport::Finding> &findings);
    // A finding handed over to be fixed is not resolved nor sent again: the
    // session that fixes it has it already.
    //
    // resolveRequested() with `findings` not handed over yet, which are then
    // resolving; nothing when there are none.
    void resolve(const QList<ReviewReport::Finding> &findings);
    // `finding` was handed to be fixed (resolve()).
    bool isResolving(const ReviewReport::Finding &finding) const;
    // `findings` but those handed over to be fixed: what resolve() and
    // send() act on.
    QList<ReviewReport::Finding> notHandedOver(const QList<ReviewReport::Finding> &findings) const;
    // sendRequested() with `findings` not handed over, or with every finding
    // left not handed over (or the raw answer); nothing when there are none.
    void send(const QList<ReviewReport::Finding> &findings);
    void sendAll();

signals:
    void stateChanged(ReviewSession::State state);
    // steps() grew.
    void stepsAdded();
    // A finding was dismissed, or is resolving.
    void findingsChanged();
    // `review`, what the reviewer found in Markdown, for the session that
    // wrote the code.
    void sendRequested(const QString &review);
    // `review`, findings in Markdown, for the session that wrote the code
    // to fix.
    void resolveRequested(const QString &review);

private:
    void setState(State state);
    void onCheckFinished(const ProjectCheck::Result &result);
    void startReview(const std::optional<ReviewRequest::Check> &check);
    // The patch in a file of its own folder, for the reviewer to read;
    // nullopt (the session failed) when it cannot be written.
    std::optional<QString> writePatch();
    void onRunFinished(const AgentRun::Outcome &outcome);
    void addSteps(const QStringList &steps);
    void fail(const QString &error);
    // The findings left, in the ProblemStore.
    void publishFindings();

    QString workDir_;
    QString subject_;
    QByteArray patch_;
    QString checkCommand_;
    QString model_;
    const AgentProvider *agent_;
    QPointer<ProblemStore> problems_;
    QString reviewId_;      // the findings' set in the ProblemStore
    QString problemSource_; // what the Problems pane says told them
    ProjectCheck *check_;
    AgentRun *run_;
    std::unique_ptr<QTemporaryDir> patchFolder_;

    State state_ = State::Running;
    QElapsedTimer clock_;                        // since the check or the review started
    std::chrono::milliseconds elapsedBefore_{0}; // of the phases over: the check, before Review Anyway
    std::chrono::milliseconds elapsed_{0};       // while nothing runs
    QStringList steps_;
    std::optional<ReviewRequest::Check> failedCheck_;
    QString error_;
    QString summary_;
    QList<ReviewReport::Finding> findings_;
    QList<ReviewReport::Finding> resolving_;
    QString rawAnswer_;
};
