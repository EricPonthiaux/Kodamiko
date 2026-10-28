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
    : QWidget(parent)
    , session_(new ReviewSession(std::move(review), this))
    , pages_(new QStackedLayout(this))
    , progress_(new ReviewProgressView(session_, this))
    , title_(new QLabel(this))
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
    connect(session_, &ReviewSession::stateChanged, this, &ReviewView::onStateChanged);
    connect(session_, &ReviewSession::findingsChanged, this, &ReviewView::showFindings);
    findings_->setSession(session_);
    connect(findings_, &ReviewFindingsView::findingRequested, this,
            [this](const ReviewReport::Finding &finding) { emit openRequested(finding.path, finding.line); });
    pages_->addWidget(progress_);
    pages_->addWidget(createResults());
}

QWidget *ReviewView::createResults()
{
    results_ = new QWidget(this);
    auto *note = new QLabel(tr("A separate %1 session reviewed the code: it did not see your conversation and "
                               "could only read the project.")
                                .arg(session_->reviewerName()),
                            results_);
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::PlaceholderText);
    rawAnswerView_->setFrameShape(QFrame::NoFrame);
    rawAnswerView_->setOpenLinks(false);
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(ReviewText::errorColor(palette())));

    auto *layout = new QVBoxLayout(results_);
    layout->setContentsMargins(marginPx, marginPx, marginPx, marginPx);
    layout->addLayout(createHeader());
    layout->addWidget(note);
    layout->addWidget(error_);
    layout->addWidget(summary_);
    layout->addWidget(findings_, 1);
    layout->addWidget(rawAnswerView_, 1);
    return results_;
}

QLayout *ReviewView::createHeader()
{
    title_->setWordWrap(true);
    title_->setTextFormat(Qt::PlainText);
    AppFonts::setInterfaceFont(title_, 1.2);
    elapsedLabel_->setForegroundRole(QPalette::PlaceholderText);
    sendAll_->setToolTip(tr("Type the answer in %1's prompt, for you to complete and send").arg(session_->agentName()));

    reviewAnyway_->setToolTip(tr("Review the code although the check failed, telling the reviewer the failure"));
    connect(reviewAnyway_, &QPushButton::clicked, session_, &ReviewSession::reviewAnyway);
    connect(close_, &QPushButton::clicked, this, &ReviewView::closeRequested);
    connect(retry_, &QPushButton::clicked, session_, &ReviewSession::start);
    connect(sendAll_, &QPushButton::clicked, session_, &ReviewSession::sendAll);

    auto *header = new QHBoxLayout;
    header->addWidget(title_, 1);
    header->addWidget(elapsedLabel_);
    header->addWidget(reviewAnyway_);
    header->addWidget(retry_);
    header->addWidget(sendAll_);
    header->addWidget(close_);
    return header;
}

void ReviewView::onStateChanged(ReviewSession::State state)
{
    switch (state) {
    case ReviewSession::State::Running:
    case ReviewSession::State::Failed:
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
    for (QWidget *widget : std::initializer_list<QWidget *>{error_, summary_, findings_, rawAnswerView_, reviewAnyway_,
                                                            retry_, sendAll_, close_})
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
    title_->setText(tr("The project's check failed"));
    error_->setText(ReviewText::checkFailure(check));
    rawAnswerView_->setFont(AppFonts::codeFont(font().pointSizeF()));
    rawAnswerView_->setPlainText(check.result.lastLines(shownOutputLines));
    for (QWidget *widget : std::initializer_list<QWidget *>{error_, rawAnswerView_, reviewAnyway_, retry_, close_})
        widget->show();
}

void ReviewView::showFinished()
{
    showResults();
    title_->setText(tr("Review of %1").arg(session_->subject()));
    retry_->show();
    close_->show();
    if (session_->rawAnswer().isEmpty()) {
        const QString summary = ReviewText::summary(*session_);
        summary_->setText(summary);
        summary_->setVisible(!summary.isEmpty());
        showFindings();
        return;
    }
    rawAnswerView_->setFont(font());
    rawAnswerView_->setMarkdown(session_->rawAnswer());
    rawAnswerView_->show();
    sendAll_->show();
}

void ReviewView::showFindings()
{
    findings_->setVisible(session_->state() == ReviewSession::State::Finished && session_->rawAnswer().isEmpty() &&
                          !session_->findings().isEmpty());
}
