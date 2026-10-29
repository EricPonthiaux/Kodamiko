// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewView.h"

#include "ReviewFindingsView.h"
#include "ReviewProgressView.h"
#include "ReviewText.h"
#include "ui/AppFonts.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedLayout>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr int marginPx = 16;
// Lines of a failing check's output shown in the tab.
constexpr int shownOutputLines = 200;

QLabel *wrappedLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

} // namespace

ReviewView::ReviewView(ReviewSession::Request review, QWidget *parent)
    : ReviewView(new ReviewSession(std::move(review)), parent)
{
    session_->setParent(this);
    connect(this, &ReviewView::reviewRequested, session_, &ReviewSession::start);
    connect(this, &ReviewView::inspectRequested, this, &ReviewView::closeRequested);
}

ReviewView::ReviewView(ReviewSession *session, QWidget *parent)
    : QWidget(parent)
    , session_(session)
    , pages_(new QStackedLayout(this))
    , progress_(new ReviewProgressView(session_, this))
    , title_(new QLabel(this))
    , kind_(new QLabel(this))
    , elapsedLabel_(new QLabel(this))
    , summary_(wrappedLabel(this))
    , findings_(new ReviewFindingsView(this))
    , rawAnswerView_(new QTextBrowser(this))
    , error_(wrappedLabel(this))
    , reviewAnyway_(new QPushButton(tr("Review Anyway"), this))
    , retry_(new QPushButton(tr("Review Again"), this))
    , sendAll_(new QPushButton(tr("Send All to %1").arg(session_->agentName()), this))
    , close_(new QPushButton(tr("Close"), this))
{
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    connect(progress_, &ReviewProgressView::closeRequested, this, &ReviewView::closeRequested);
    connect(progress_, &ReviewProgressView::reviewRequested, this, &ReviewView::reviewRequested);
    connect(session_, &ReviewSession::stateChanged, this, &ReviewView::onStateChanged);
    connect(session_, &ReviewSession::findingsChanged, this, &ReviewView::showFindings);
    findings_->setSession(session_);
    connect(findings_, &ReviewFindingsView::findingRequested, this,
            [this](const ReviewReport::Finding &finding) { emit openRequested(finding.path, finding.line); });
    connect(findings_, &ReviewFindingsView::findingRequested, this, &ReviewView::findingRequested);
    pages_->addWidget(progress_);
    pages_->addWidget(createResults());
    pages_->addWidget(createCleanResult());
    onStateChanged(session_->state());
}

QWidget *ReviewView::createResults()
{
    results_ = new QWidget(this);
    handoffNote_ = new QLabel(
        tr("A handoff to %1 starts a follow-up. It does not mark a finding as fixed.").arg(session_->agentName()),
        results_);
    handoffNote_->setWordWrap(true);
    handoffNote_->setObjectName(QStringLiteral("reviewMuted"));
    rawAnswerView_->setFrameShape(QFrame::NoFrame);
    rawAnswerView_->setOpenLinks(false);
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(ReviewText::errorColor(palette())));

    resultsColumn_ = new QWidget(results_);
    resultsColumn_->setMaximumWidth(1060);
    auto *outer = new QHBoxLayout(results_);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(resultsColumn_);
    auto *layout = new QVBoxLayout(resultsColumn_);
    resultsLayout_ = layout;
    layout->setContentsMargins(32, 30, 32, 24);
    layout->setSpacing(marginPx);
    failureSymbol_ = new QLabel(QStringLiteral("!"), resultsColumn_);
    failureSymbol_->setAlignment(Qt::AlignCenter);
    failureSymbol_->setFixedSize(48, 48);
    failureSymbol_->setObjectName(QStringLiteral("reviewFailureSymbol"));
    AppFonts::setInterfaceFont(failureSymbol_, 2.2, QFont::Normal);
    layout->addWidget(failureSymbol_);
    layout->addLayout(createHeader());
    layout->addWidget(error_);
    layout->addWidget(summary_);
    layout->addWidget(findings_, 1);
    layout->addWidget(rawAnswerView_, 1);
    layout->addWidget(handoffNote_);
    // Takes the height left when neither the findings nor the answer show (a
    // review without findings): the texts stay at the top, not spread out.
    layout->addStretch();
    return results_;
}

QWidget *ReviewView::createCleanResult()
{
    clean_ = new QWidget(this);
    auto *column = new QWidget(clean_);
    column->setMaximumWidth(620);
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(20);
    auto *symbol = new QLabel(QStringLiteral("✓"), column);
    symbol->setObjectName(QStringLiteral("reviewSuccessSymbol"));
    symbol->setAlignment(Qt::AlignCenter);
    symbol->setFixedSize(56, 56);
    AppFonts::setInterfaceFont(symbol, 2.1, QFont::Normal);
    layout->addWidget(symbol, 0, Qt::AlignHCenter);
    cleanElapsed_ = new QLabel(column);
    cleanElapsed_->setObjectName(QStringLiteral("reviewKind"));
    cleanElapsed_->setAlignment(Qt::AlignCenter);
    AppFonts::setInterfaceFont(cleanElapsed_, 0.85, QFont::DemiBold, 1.2);
    layout->addWidget(cleanElapsed_);
    auto *title = wrappedLabel(column);
    title->setObjectName(QStringLiteral("reviewCleanTitle"));
    title->setText(tr("No findings this time."));
    title->setAlignment(Qt::AlignCenter);
    AppFonts::setInterfaceFont(title, 2.0, QFont::Normal);
    layout->addWidget(title);
    cleanSummary_ = wrappedLabel(column);
    cleanSummary_->setAlignment(Qt::AlignCenter);
    cleanSummary_->setObjectName(QStringLiteral("reviewMuted"));
    layout->addWidget(cleanSummary_);
    cleanWarning_ = wrappedLabel(column);
    cleanWarning_->setText(tr("The project check failed. It still needs attention before committing."));
    cleanWarning_->setAlignment(Qt::AlignCenter);
    cleanWarning_->setObjectName(QStringLiteral("reviewWarning"));
    layout->addWidget(cleanWarning_);
    auto *scope = new QLabel(session_->subject(), column);
    scope->setTextFormat(Qt::PlainText);
    scope->setWordWrap(true);
    scope->setAlignment(Qt::AlignCenter);
    scope->setObjectName(QStringLiteral("reviewMuted"));
    layout->addWidget(scope);
    auto *actions = new QHBoxLayout;
    actions->addStretch();
    auto *inspect = new QPushButton(tr("Inspect changes"), column);
    inspect->setObjectName(QStringLiteral("reviewPrimary"));
    connect(inspect, &QPushButton::clicked, this, &ReviewView::inspectRequested);
    actions->addWidget(inspect);
    auto *retry = new QPushButton(tr("Review Again"), column);
    connect(retry, &QPushButton::clicked, this, &ReviewView::reviewRequested);
    actions->addWidget(retry);
    actions->addStretch();
    layout->addLayout(actions);
    auto *row = new QHBoxLayout;
    row->addWidget(column);
    auto *outer = new QVBoxLayout(clean_);
    outer->addStretch(2);
    outer->addLayout(row);
    outer->addStretch(3);
    return clean_;
}

QLayout *ReviewView::createHeader()
{
    title_->setWordWrap(true);
    title_->setTextFormat(Qt::PlainText);
    AppFonts::setInterfaceFont(title_, 1.9, QFont::Normal);
    kind_->setObjectName(QStringLiteral("reviewKind"));
    AppFonts::setInterfaceFont(kind_, 0.85, QFont::DemiBold, 1.2);
    elapsedLabel_->setObjectName(QStringLiteral("reviewMuted"));
    sendAll_->setToolTip(tr("Type the answer in %1's prompt, for you to complete and send").arg(session_->agentName()));

    reviewAnyway_->setToolTip(tr("Review the code although the check failed, telling the reviewer the failure"));
    connect(reviewAnyway_, &QPushButton::clicked, session_, &ReviewSession::reviewAnyway);
    connect(close_, &QPushButton::clicked, this, &ReviewView::closeRequested);
    connect(retry_, &QPushButton::clicked, this, &ReviewView::reviewRequested);
    connect(sendAll_, &QPushButton::clicked, session_, &ReviewSession::sendAll);

    resultActions_ = new QWidget(this);
    auto *actions = new QHBoxLayout(resultActions_);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->addWidget(reviewAnyway_);
    actions->addWidget(retry_);
    actions->addWidget(sendAll_);
    actions->addWidget(close_);
    headingLayout_ = new QHBoxLayout;
    headingLayout_->addWidget(title_, 1);
    headingLayout_->addWidget(resultActions_);
    auto *eyebrow = new QHBoxLayout;
    eyebrow->addWidget(kind_);
    eyebrow->addStretch();
    eyebrow->addWidget(elapsedLabel_);
    auto *layout = new QVBoxLayout;
    layout->setSpacing(12);
    layout->addLayout(eyebrow);
    layout->addLayout(headingLayout_);
    auto *scope = new QLabel(session_->subject(), this);
    scope->setWordWrap(true);
    scope->setTextFormat(Qt::PlainText);
    scope->setObjectName(QStringLiteral("reviewMuted"));
    layout->addWidget(scope);
    return layout;
}

void ReviewView::onStateChanged(ReviewSession::State state)
{
    switch (state) {
    case ReviewSession::State::Running:
    case ReviewSession::State::Failed:
    case ReviewSession::State::Interrupted:
        // The progress page follows the run, and shows its failure.
        pages_->setCurrentWidget(progress_);
        return;
    case ReviewSession::State::CheckFailed:
        showCheckFailure();
        return;
    case ReviewSession::State::Finished:
        showFinished();
        return;
    }
}

void ReviewView::showResults()
{
    for (QWidget *widget : std::initializer_list<QWidget *>{failureSymbol_, handoffNote_, error_, summary_, findings_,
                                                            rawAnswerView_, reviewAnyway_, retry_, sendAll_, close_})
        widget->hide();
    elapsedLabel_->setText(ReviewText::elapsed(session_->elapsed()));
    pages_->setCurrentWidget(results_);
}

void ReviewView::showCheckFailure()
{
    if (!session_->failedCheck())
        return;
    const ReviewRequest::Check &check = *session_->failedCheck();
    showResults();
    resultsColumn_->setMaximumWidth(760);
    failureSymbol_->show();
    resultsLayout_->insertWidget(resultsLayout_->indexOf(rawAnswerView_) + 1, resultActions_, 0, Qt::AlignLeft);
    kind_->setText(tr("Project check failed").toUpper());
    title_->setText(tr("The check needs attention first."));
    error_->setText(ReviewText::checkFailure(check));
    rawAnswerView_->setFont(AppFonts::codeFont(font().pointSizeF()));
    rawAnswerView_->setPlainText(check.result.lastLines(shownOutputLines));
    rawAnswerView_->setMaximumHeight(fontMetrics().lineSpacing() * 12);
    for (QWidget *widget : std::initializer_list<QWidget *>{error_, rawAnswerView_, reviewAnyway_, retry_, close_})
        widget->show();
}

void ReviewView::showFinished()
{
    if (session_->rawAnswer().isEmpty() && session_->reportedFindingCount() == 0) {
        cleanSummary_->setText(
            session_->summary().isEmpty()
                ? tr("%1 did not identify a concrete issue in these changes.").arg(session_->agentName())
                : session_->summary());
        cleanElapsed_->setText(tr("Review complete · %1").arg(ReviewText::elapsed(session_->elapsed())).toUpper());
        cleanWarning_->setVisible(session_->failedCheck().has_value());
        pages_->setCurrentWidget(clean_);
        return;
    }
    showResults();
    resultsColumn_->setMaximumWidth(1060);
    headingLayout_->addWidget(resultActions_);
    rawAnswerView_->setMaximumHeight(QWIDGETSIZE_MAX);
    kind_->setText(tr("Review complete").toUpper());
    title_->setText(session_->reportedFindingCount() > 0 ? tr("Findings to look through")
                                                         : tr("No findings this time."));
    retry_->show();
    close_->show();
    handoffNote_->setVisible(session_->reportedFindingCount() > 0);
    if (session_->failedCheck()) {
        error_->setText(tr("The project check failed. These findings do not replace a passing check."));
        error_->show();
    }
    if (session_->rawAnswer().isEmpty()) {
        const QString summary = ReviewText::summary(*session_);
        summary_->setText(summary);
        summary_->setVisible(!summary.isEmpty());
        showFindings();
        return;
    }
    title_->setText(tr("Review of %1").arg(session_->subject()));
    rawAnswerView_->setFont(font());
    rawAnswerView_->setMarkdown(session_->rawAnswer());
    rawAnswerView_->show();
    sendAll_->show();
}

void ReviewView::showFindings()
{
    findings_->setVisible(session_->state() == ReviewSession::State::Finished && session_->rawAnswer().isEmpty() &&
                          session_->reportedFindingCount() > 0);
}
