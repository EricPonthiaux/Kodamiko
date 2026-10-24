// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewView.h"

#include "AgentProvider.h"
#include "ReviewFindingCard.h"
#include "diagnostics/ProblemStore.h"
#include "ui/AppFonts.h"
#include "ui/Spinner.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

// The reviewer may read a good part of the project.
constexpr std::chrono::minutes timeout{15};
constexpr int shownSteps = 6;
constexpr int marginPx = 16;
constexpr int cardSpacingPx = 8;
// Lines of a failing check's output shown in the tab.
constexpr int shownOutputLines = 200;

QString errorColor(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128 ? QStringLiteral("#e06c75") : QStringLiteral("#e45649");
}

// Why there is no review: the agent's failure or its error output, the most
// telling first.
QString failureMessage(const QString &agentName, const AgentRun::Outcome &outcome)
{
    if (!outcome.answer.isEmpty())
        return outcome.answer;
    if (!outcome.errorOutput.isEmpty())
        return outcome.errorOutput;
    return ReviewView::tr("%1 gave no answer.").arg(agentName);
}

QLabel *wrappedLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

} // namespace

ReviewView::ReviewView(Request review, QWidget *parent)
    : QWidget(parent)
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
    , clock_(new QTimer(this))
    , title_(new QLabel(this))
    , elapsedLabel_(new QLabel(this))
    , summary_(wrappedLabel(this))
    , rawAnswerView_(new QTextBrowser(this))
    , error_(wrappedLabel(this))
    , cancel_(new QPushButton(tr("Cancel"), this))
    , reviewAnyway_(new QPushButton(tr("Review Anyway"), this))
    , retry_(new QPushButton(tr("Review Again"), this))
    , sendAll_(new QPushButton(tr("Send All to %1").arg(agent_->name()), this))
    , close_(new QPushButton(tr("Close"), this))
{
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    const QString reviewer = model_.isEmpty() ? agent_->name() : tr("%1 (model: %2)").arg(agent_->name(), model_);
    auto *note = new QLabel(tr("A separate %1 session reviews the code: it does not see your conversation and "
                               "may only read the project.")
                                .arg(reviewer),
                            this);
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::PlaceholderText);
    rawAnswerView_->setFrameShape(QFrame::NoFrame);
    rawAnswerView_->setOpenLinks(false);
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(errorColor(palette())));

    connect(clock_, &QTimer::timeout, this, &ReviewView::updateElapsed);
    clock_->setInterval(1000);
    connect(check_, &ProjectCheck::finished, this, &ReviewView::onCheckFinished);
    connect(run_, &AgentRun::stepsAdded, this, &ReviewView::addSteps);
    connect(run_, &AgentRun::finished, this, &ReviewView::onRunFinished);
    connect(run_, &AgentRun::failed, this, &ReviewView::fail);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(marginPx, marginPx, marginPx, marginPx);
    layout->addLayout(createHeader());
    layout->addWidget(note);
    layout->addWidget(error_);
    layout->addWidget(summary_);
    layout->addWidget(createProgress(), 1);
    layout->addWidget(createFindingsArea(), 1);
    layout->addWidget(rawAnswerView_, 1);
}

ReviewView::~ReviewView()
{
    if (problems_)
        problems_->setReviewProblems(reviewId_, {});
}

QLayout *ReviewView::createHeader()
{
    title_->setWordWrap(true);
    title_->setTextFormat(Qt::PlainText);
    AppFonts::setInterfaceFont(title_, 1.2);
    elapsedLabel_->setForegroundRole(QPalette::PlaceholderText);
    sendAll_->setToolTip(
        tr("Type every finding left in %1's prompt, for you to complete and send").arg(agent_->name()));

    reviewAnyway_->setToolTip(tr("Review the code although the check failed, telling the reviewer the failure"));
    connect(cancel_, &QPushButton::clicked, this, [this] {
        check_->stop();
        run_->stop();
        emit closeRequested();
    });
    connect(reviewAnyway_, &QPushButton::clicked, this, [this] {
        showProgress();
        clock_->start();
        startReview(failedCheck_);
    });
    connect(close_, &QPushButton::clicked, this, &ReviewView::closeRequested);
    connect(retry_, &QPushButton::clicked, this, &ReviewView::start);
    connect(sendAll_, &QPushButton::clicked, this, [this] {
        emit sendRequested(rawAnswer_.isEmpty() ? ReviewReport::toMarkdown(findings(), workDir_) : rawAnswer_);
    });

    auto *header = new QHBoxLayout;
    header->addWidget(title_, 1);
    header->addWidget(elapsedLabel_);
    header->addWidget(cancel_);
    header->addWidget(reviewAnyway_);
    header->addWidget(retry_);
    header->addWidget(sendAll_);
    header->addWidget(close_);
    return header;
}

QWidget *ReviewView::createProgress()
{
    progress_ = new QWidget(this);
    spinner_ = new Spinner(progress_);
    stepsLabel_ = new QLabel(progress_);
    stepsLabel_->setAlignment(Qt::AlignCenter);
    stepsLabel_->setTextFormat(Qt::RichText);
    stepsLabel_->setWordWrap(true);
    auto *layout = new QVBoxLayout(progress_);
    layout->addStretch(2);
    layout->addWidget(spinner_, 0, Qt::AlignHCenter);
    layout->addWidget(stepsLabel_);
    layout->addStretch(3);
    return progress_;
}

QWidget *ReviewView::createFindingsArea()
{
    auto *container = new QWidget;
    findingsLayout_ = new QVBoxLayout(container);
    findingsLayout_->setContentsMargins(0, 0, 0, 0);
    findingsLayout_->setSpacing(cardSpacingPx);
    findingsLayout_->addStretch();
    findingsArea_ = new QScrollArea(this);
    findingsArea_->setFrameShape(QFrame::NoFrame);
    findingsArea_->setWidgetResizable(true);
    findingsArea_->setWidget(container);
    return findingsArea_;
}

void ReviewView::start()
{
    check_->stop();
    run_->stop();
    clearFindings();
    steps_.clear();
    rawAnswer_.clear();
    failedCheck_.reset();
    title_->setText(tr("Reviewing %1").arg(subject_));
    showProgress();
    elapsed_.start();
    updateElapsed();
    clock_->start();
    if (checkCommand_.isEmpty()) {
        startReview(std::nullopt);
        return;
    }
    addSteps({tr("Checking: %1").arg(checkCommand_)});
    check_->start(checkCommand_, workDir_);
}

void ReviewView::showProgress()
{
    for (QWidget *widget : std::initializer_list<QWidget *>{error_, summary_, findingsArea_, rawAnswerView_,
                                                            reviewAnyway_, retry_, sendAll_, close_})
        widget->hide();
    cancel_->show();
    progress_->show();
    spinner_->setSpinning(true);
}

void ReviewView::onCheckFinished(const ProjectCheck::Result &result)
{
    const ReviewRequest::Check check{.command = checkCommand_, .result = result};
    if (result.hasPassed())
        startReview(check);
    else
        showCheckFailure(check);
}

void ReviewView::showCheckFailure(const ReviewRequest::Check &check)
{
    failedCheck_ = check;
    updateElapsed();
    clock_->stop();
    spinner_->setSpinning(false);
    title_->setText(tr("The project's check failed"));
    error_->setText(tr("`%1` failed with exit code %2: fix the code, or review it anyway.")
                        .arg(check.command, QString::number(check.result.exitCode)));
    rawAnswerView_->setFont(AppFonts::codeFont(font().pointSizeF()));
    rawAnswerView_->setPlainText(check.result.lastLines(shownOutputLines));
    progress_->hide();
    cancel_->hide();
    for (QWidget *widget : std::initializer_list<QWidget *>{error_, rawAnswerView_, reviewAnyway_, retry_, close_})
        widget->show();
}

void ReviewView::startReview(const std::optional<ReviewRequest::Check> &check)
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

std::optional<QString> ReviewView::writePatch()
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

void ReviewView::onRunFinished(const AgentRun::Outcome &outcome)
{
    const bool hasReview = !outcome.failed && !outcome.answer.isEmpty();
    if (!hasReview) {
        fail(failureMessage(agent_->name(), outcome));
        return;
    }
    if (const std::optional<ReviewReport::Report> report = ReviewReport::parse(outcome.answer, workDir_))
        showReport(*report);
    else
        showRawAnswer(outcome.answer);
}

void ReviewView::addSteps(const QStringList &steps)
{
    steps_ += steps;
    // The last steps, the current one in full color with an ellipsis.
    const QStringList shown = steps_.mid(std::max<qsizetype>(0, steps_.size() - shownSteps));
    const QString muted = palette().color(QPalette::PlaceholderText).name();
    QStringList lines;
    for (qsizetype i = 0; i < shown.size(); ++i) {
        const QString text = shown.at(i).toHtmlEscaped();
        lines.append(i + 1 == shown.size() ? text + QStringLiteral("…")
                                           : QStringLiteral("<span style=\"color:%1\">%2</span>").arg(muted, text));
    }
    stepsLabel_->setText(lines.join(QStringLiteral("<br>")));
}

void ReviewView::showReport(const ReviewReport::Report &report)
{
    showFinished();
    for (const ReviewReport::Finding &finding : report.findings)
        addCard(finding);
    publishFindings();
    QString summary = report.summary;
    if (report.findings.isEmpty())
        summary = summary.isEmpty() ? tr("No problems found.") : summary + QLatin1Char('\n') + tr("No problems found.");
    summary_->setText(summary);
    summary_->setVisible(!summary.isEmpty());
    findingsArea_->setVisible(!report.findings.isEmpty());
    sendAll_->setVisible(!report.findings.isEmpty());
}

void ReviewView::showRawAnswer(const QString &answer)
{
    showFinished();
    rawAnswer_ = answer;
    rawAnswerView_->setFont(font());
    rawAnswerView_->setMarkdown(answer);
    rawAnswerView_->show();
    sendAll_->show();
}

void ReviewView::showFinished()
{
    updateElapsed();
    clock_->stop();
    spinner_->setSpinning(false);
    title_->setText(tr("Review of %1").arg(subject_));
    progress_->hide();
    cancel_->hide();
    retry_->show();
    close_->show();
}

void ReviewView::addCard(const ReviewReport::Finding &finding)
{
    const QString location = QStringLiteral("%1:%2").arg(
        QDir::toNativeSeparators(QDir(workDir_).relativeFilePath(finding.path)), QString::number(finding.line));
    auto *card = new ReviewFindingCard(finding, location, agent_->name(), findingsArea_->widget());
    // Before the stretch that keeps the cards at the top.
    findingsLayout_->insertWidget(findingsLayout_->count() - 1, card);
    entries_.append({finding, card});
    connect(card, &ReviewFindingCard::openRequested, this,
            [this, finding] { emit openRequested(finding.path, finding.line); });
    connect(card, &ReviewFindingCard::sendRequested, this,
            [this, finding] { emit sendRequested(ReviewReport::toMarkdown({finding}, workDir_)); });
    connect(card, &ReviewFindingCard::dismissRequested, this, [this, card] { dismiss(card); });
}

void ReviewView::dismiss(ReviewFindingCard *card)
{
    entries_.removeIf([card](const Entry &entry) { return entry.card == card; });
    card->deleteLater();
    publishFindings();
    const bool hasFindings = !entries_.isEmpty();
    findingsArea_->setVisible(hasFindings);
    sendAll_->setVisible(hasFindings);
}

void ReviewView::clearFindings()
{
    for (const Entry &entry : std::as_const(entries_))
        entry.card->deleteLater();
    entries_.clear();
    publishFindings();
}

void ReviewView::publishFindings()
{
    if (!problems_)
        return;
    Problems problems;
    for (const Entry &entry : std::as_const(entries_))
        problems.append(ReviewReport::toProblem(entry.finding, problemSource_));
    problems_->setReviewProblems(reviewId_, problems);
}

QList<ReviewReport::Finding> ReviewView::findings() const
{
    QList<ReviewReport::Finding> findings;
    for (const Entry &entry : entries_)
        findings.append(entry.finding);
    return findings;
}

void ReviewView::fail(const QString &error)
{
    updateElapsed();
    clock_->stop();
    check_->stop();
    run_->stop();
    spinner_->setSpinning(false);
    title_->setText(tr("%1 could not review %2").arg(agent_->name(), subject_));
    error_->setText(error);
    error_->show();
    progress_->hide();
    cancel_->hide();
    retry_->show();
    close_->show();
}

void ReviewView::updateElapsed()
{
    const qint64 seconds = elapsed_.elapsed() / 1000;
    elapsedLabel_->setText(QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')));
}
