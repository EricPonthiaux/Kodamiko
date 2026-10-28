// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewSession.h"

#include "AgentProvider.h"
#include "diagnostics/ProblemStore.h"

#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>

#include <utility>

namespace {

// The reviewer may read a good part of the project.
constexpr std::chrono::minutes timeout{15};

// Why there is no review: the agent's failure or its error output, the most
// telling first.
QString failureMessage(const QString &agentName, const AgentRun::Outcome &outcome)
{
    if (!outcome.answer.isEmpty())
        return outcome.answer;
    if (!outcome.errorOutput.isEmpty())
        return outcome.errorOutput;
    return ReviewSession::tr("%1 gave no answer.").arg(agentName);
}

} // namespace

ReviewSession::ReviewSession(Request review, QObject *parent)
    : QObject(parent)
    , workDir_(std::move(review.workDir))
    , subject_(std::move(review.subject))
    , patch_(std::move(review.patch))
    , checkCommand_(std::move(review.checkCommand))
    , model_(std::move(review.model))
    , agent_(review.agent)
    , problems_(review.problems)
    , reviewId_(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , problemSource_(tr("%1 review").arg(agent_->name()))
    , check_(new ProjectCheck(this))
    , run_(new AgentRun(agent_, workDir_, this))
{
    connect(check_, &ProjectCheck::finished, this, &ReviewSession::onCheckFinished);
    connect(run_, &AgentRun::stepsAdded, this, &ReviewSession::addSteps);
    connect(run_, &AgentRun::finished, this, &ReviewSession::onRunFinished);
    connect(run_, &AgentRun::failed, this, &ReviewSession::fail);
}

ReviewSession::~ReviewSession()
{
    if (problems_)
        problems_->setReviewProblems(reviewId_, {});
}

QString ReviewSession::agentName() const
{
    return agent_->name();
}

QString ReviewSession::reviewerName() const
{
    return model_.isEmpty() ? agent_->name() : tr("%1 (model: %2)").arg(agent_->name(), model_);
}

std::chrono::milliseconds ReviewSession::elapsed() const
{
    if (state_ == State::Running)
        return elapsedBefore_ + std::chrono::milliseconds(clock_.elapsed());
    return elapsed_;
}

void ReviewSession::start()
{
    stop();
    steps_.clear();
    failedCheck_.reset();
    error_.clear();
    summary_.clear();
    findings_.clear();
    resolving_.clear();
    rawAnswer_.clear();
    publishFindings();
    elapsedBefore_ = std::chrono::milliseconds(0);
    clock_.start();
    setState(State::Running);
    if (checkCommand_.isEmpty()) {
        startReview(std::nullopt);
        return;
    }
    addSteps({tr("Checking: %1").arg(checkCommand_)});
    check_->start(checkCommand_, workDir_);
}

void ReviewSession::reviewAnyway()
{
    if (state_ != State::CheckFailed)
        return;
    elapsedBefore_ = elapsed_;
    clock_.start();
    setState(State::Running);
    startReview(failedCheck_);
}

void ReviewSession::stop()
{
    check_->stop();
    run_->stop();
}

void ReviewSession::setState(State state)
{
    if (state != State::Running)
        elapsed_ = elapsedBefore_ + std::chrono::milliseconds(clock_.elapsed());
    state_ = state;
    emit stateChanged(state);
}

void ReviewSession::onCheckFinished(const ProjectCheck::Result &result)
{
    const ReviewRequest::Check check{.command = checkCommand_, .result = result};
    if (result.hasPassed()) {
        startReview(check);
        return;
    }
    failedCheck_ = check;
    setState(State::CheckFailed);
}

void ReviewSession::startReview(const std::optional<ReviewRequest::Check> &check)
{
    const std::optional<QString> patchPath = writePatch();
    if (!patchPath)
        return;
    addSteps({tr("Starting %1").arg(agent_->name())});
    QString launchError;
    const std::optional<ProcessSpec> spec = agent_->reviewRunProcess(
        {.workingDirectory = workDir_,
         .prompt = ReviewRequest::prompt({.subject = subject_, .patchPath = *patchPath, .check = check}),
         .answerSchema = ReviewReport::schema(),
         .model = model_},
        &launchError);
    if (!spec) {
        fail(launchError);
        return;
    }
    run_->start(*spec, timeout);
}

std::optional<QString> ReviewSession::writePatch()
{
    if (patch_.isEmpty())
        return QString();
    patchFolder_ = std::make_unique<QTemporaryDir>();
    QSaveFile file(patchFolder_->filePath(QStringLiteral("changes.diff")));
    const bool isWritten =
        patchFolder_->isValid() && file.open(QIODevice::WriteOnly) && file.write(patch_) >= 0 && file.commit();
    if (!isWritten) {
        fail(tr("Could not write the changes to review in %1: %2").arg(file.fileName(), file.errorString()));
        return std::nullopt;
    }
    return file.fileName();
}

void ReviewSession::onRunFinished(const AgentRun::Outcome &outcome)
{
    const bool hasReview = !outcome.failed && !outcome.answer.isEmpty();
    if (!hasReview) {
        fail(failureMessage(agent_->name(), outcome));
        return;
    }
    if (const std::optional<ReviewReport::Report> report = ReviewReport::parse(outcome.answer, workDir_)) {
        summary_ = report->summary;
        findings_ = report->findings;
        publishFindings();
    } else {
        rawAnswer_ = outcome.answer;
    }
    setState(State::Finished);
}

void ReviewSession::addSteps(const QStringList &steps)
{
    steps_ += steps;
    emit stepsAdded();
}

void ReviewSession::fail(const QString &error)
{
    stop();
    error_ = error;
    setState(State::Failed);
}

void ReviewSession::dismiss(const QList<ReviewReport::Finding> &findings)
{
    const qsizetype removed =
        findings_.removeIf([&findings](const ReviewReport::Finding &finding) { return findings.contains(finding); });
    if (removed == 0)
        return;
    publishFindings();
    emit findingsChanged();
}

void ReviewSession::resolve(const QList<ReviewReport::Finding> &findings)
{
    const QList<ReviewReport::Finding> handedOver = notHandedOver(findings);
    if (handedOver.isEmpty())
        return;
    resolving_ += handedOver;
    emit resolveRequested(ReviewReport::toMarkdown(handedOver, workDir_));
    emit findingsChanged();
}

bool ReviewSession::isResolving(const ReviewReport::Finding &finding) const
{
    return resolving_.contains(finding);
}

QList<ReviewReport::Finding> ReviewSession::notHandedOver(const QList<ReviewReport::Finding> &findings) const
{
    QList<ReviewReport::Finding> left;
    for (const ReviewReport::Finding &finding : findings) {
        if (!resolving_.contains(finding) && !left.contains(finding))
            left.append(finding);
    }
    return left;
}

void ReviewSession::send(const QList<ReviewReport::Finding> &findings)
{
    const QList<ReviewReport::Finding> sent = notHandedOver(findings);
    if (!sent.isEmpty())
        emit sendRequested(ReviewReport::toMarkdown(sent, workDir_));
}

void ReviewSession::sendAll()
{
    if (!rawAnswer_.isEmpty()) {
        emit sendRequested(rawAnswer_);
        return;
    }
    send(findings_);
}

void ReviewSession::publishFindings()
{
    if (!problems_)
        return;
    Problems problems;
    for (const ReviewReport::Finding &finding : std::as_const(findings_))
        problems.append(ReviewReport::toProblem(finding, problemSource_));
    problems_->setReviewProblems(reviewId_, problems);
}
