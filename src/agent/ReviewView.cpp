// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewView.h"

#include "ReviewFindingCard.h"
#include "ReviewText.h"
#include "ui/AppFonts.h"
#include "ui/Spinner.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>
#include <utility>

namespace {

constexpr int shownSteps = 6;
constexpr int marginPx = 16;
constexpr int cardSpacingPx = 8;
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
    , clock_(new QTimer(this))
    , title_(new QLabel(this))
    , elapsedLabel_(new QLabel(this))
    , summary_(wrappedLabel(this))
    , rawAnswerView_(new QTextBrowser(this))
    , error_(wrappedLabel(this))
    , cancel_(new QPushButton(tr("Cancel"), this))
    , reviewAnyway_(new QPushButton(tr("Review Anyway"), this))
    , retry_(new QPushButton(tr("Review Again"), this))
    , sendAll_(new QPushButton(tr("Send All to %1").arg(session_->agentName()), this))
    , close_(new QPushButton(tr("Close"), this))
{
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    auto *note = new QLabel(tr("A separate %1 session reviews the code: it does not see your conversation and "
                               "may only read the project.")
                                .arg(session_->reviewerName()),
                            this);
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::PlaceholderText);
    rawAnswerView_->setFrameShape(QFrame::NoFrame);
    rawAnswerView_->setOpenLinks(false);
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(ReviewText::errorColor(palette())));

    connect(clock_, &QTimer::timeout, this, &ReviewView::updateElapsed);
    clock_->setInterval(1000);
    connect(session_, &ReviewSession::stateChanged, this, &ReviewView::onStateChanged);
    connect(session_, &ReviewSession::stepsAdded, this, &ReviewView::showSteps);
    connect(session_, &ReviewSession::findingsChanged, this, &ReviewView::showFindings);

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

QLayout *ReviewView::createHeader()
{
    title_->setWordWrap(true);
    title_->setTextFormat(Qt::PlainText);
    AppFonts::setInterfaceFont(title_, 1.2);
    elapsedLabel_->setForegroundRole(QPalette::PlaceholderText);
    sendAll_->setToolTip(
        tr("Type every finding left in %1's prompt, for you to complete and send").arg(session_->agentName()));

    reviewAnyway_->setToolTip(tr("Review the code although the check failed, telling the reviewer the failure"));
    connect(cancel_, &QPushButton::clicked, this, [this] {
        session_->stop();
        emit closeRequested();
    });
    connect(reviewAnyway_, &QPushButton::clicked, session_, &ReviewSession::reviewAnyway);
    connect(close_, &QPushButton::clicked, this, &ReviewView::closeRequested);
    connect(retry_, &QPushButton::clicked, session_, &ReviewSession::start);
    connect(sendAll_, &QPushButton::clicked, session_, &ReviewSession::sendAll);

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

void ReviewView::onStateChanged(ReviewSession::State state)
{
    updateElapsed();
    switch (state) {
    case ReviewSession::State::Running:
        showProgress();
        return;
    case ReviewSession::State::CheckFailed:
        showCheckFailure();
        return;
    case ReviewSession::State::Finished:
        showFinished();
        return;
    case ReviewSession::State::Failed:
        showFailure();
        return;
    }
}

void ReviewView::showProgress()
{
    for (QWidget *widget : std::initializer_list<QWidget *>{error_, summary_, findingsArea_, rawAnswerView_,
                                                            reviewAnyway_, retry_, sendAll_, close_})
        widget->hide();
    title_->setText(tr("Reviewing %1").arg(session_->subject()));
    showSteps();
    cancel_->show();
    progress_->show();
    spinner_->setSpinning(true);
    clock_->start();
}

void ReviewView::showCheckFailure()
{
    if (!session_->failedCheck())
        return;
    const ReviewRequest::Check &check = *session_->failedCheck();
    clock_->stop();
    spinner_->setSpinning(false);
    title_->setText(tr("The project's check failed"));
    error_->setText(ReviewText::checkFailure(check));
    rawAnswerView_->setFont(AppFonts::codeFont(font().pointSizeF()));
    rawAnswerView_->setPlainText(check.result.lastLines(shownOutputLines));
    progress_->hide();
    cancel_->hide();
    for (QWidget *widget : std::initializer_list<QWidget *>{error_, rawAnswerView_, reviewAnyway_, retry_, close_})
        widget->show();
}

void ReviewView::showFinished()
{
    clock_->stop();
    spinner_->setSpinning(false);
    title_->setText(tr("Review of %1").arg(session_->subject()));
    progress_->hide();
    cancel_->hide();
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

void ReviewView::showFailure()
{
    clock_->stop();
    spinner_->setSpinning(false);
    title_->setText(tr("%1 could not review %2").arg(session_->agentName(), session_->subject()));
    error_->setText(session_->error());
    error_->show();
    progress_->hide();
    cancel_->hide();
    retry_->show();
    close_->show();
}

void ReviewView::showSteps()
{
    stepsLabel_->setText(ReviewText::lastSteps(session_->steps(), shownSteps, palette()));
}

void ReviewView::showFindings()
{
    // All but the stretch.
    while (findingsLayout_->count() > 1) {
        const std::unique_ptr<QLayoutItem> item(findingsLayout_->takeAt(0));
        item->widget()->hide();
        item->widget()->deleteLater();
    }
    const QDir project(session_->workDir());
    for (const ReviewReport::Finding &finding : session_->findings()) {
        const QString location = QStringLiteral("%1:%2").arg(
            QDir::toNativeSeparators(project.relativeFilePath(finding.path)), QString::number(finding.line));
        auto *card = new ReviewFindingCard(finding, location, session_->agentName(), findingsArea_->widget());
        // Before the stretch that keeps the cards at the top.
        findingsLayout_->insertWidget(findingsLayout_->count() - 1, card);
        connect(card, &ReviewFindingCard::openRequested, this,
                [this, finding] { emit openRequested(finding.path, finding.line); });
        connect(card, &ReviewFindingCard::sendRequested, session_, [this, finding] { session_->send({finding}); });
        // Queued: dismissing rebuilds the cards, this one among them.
        connect(
            card, &ReviewFindingCard::dismissRequested, session_, [this, finding] { session_->dismiss({finding}); },
            Qt::QueuedConnection);
    }
    const bool hasFindings = !session_->findings().isEmpty();
    findingsArea_->setVisible(hasFindings);
    sendAll_->setVisible(hasFindings);
}

void ReviewView::updateElapsed()
{
    elapsedLabel_->setText(ReviewText::elapsed(session_->elapsed()));
}
