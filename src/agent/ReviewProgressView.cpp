// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewProgressView.h"

#include "ReviewSketch.h"
#include "ReviewText.h"
#include "ui/AppFonts.h"
#include "ui/StepTimeline.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// As the tab of a diagram being drawn (DiagramGenerationView).
constexpr int contentWidthPx = 710;
constexpr qreal kindScale = 0.85;
constexpr qreal kindLetterSpacingPx = 1.2;
constexpr qreal titleScale = 1.65;
constexpr int spacingPx = 8;
constexpr int sectionSpacingPx = 16;

QLabel *wrappedLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

} // namespace

ReviewProgressView::ReviewProgressView(ReviewSession *session, QWidget *parent)
    : QWidget(parent)
    , session_(session)
    , clock_(new QTimer(this))
    , sketch_(new ReviewSketch(this))
    , title_(wrappedLabel(this))
    , steps_(new StepTimeline(this))
    , elapsedLabel_(new QLabel(this))
    , error_(wrappedLabel(this))
    , cancel_(new QPushButton(tr("Stop review"), this))
    , retry_(new QPushButton(tr("Review Again"), this))
    , close_(new QPushButton(tr("Close"), this))
{
    kind_ = new QLabel(this);
    kind_->setObjectName(QStringLiteral("reviewKind"));
    AppFonts::setInterfaceFont(kind_, kindScale, QFont::DemiBold, kindLetterSpacingPx);
    elapsedLabel_->setObjectName(QStringLiteral("reviewMuted"));
    elapsedLabel_->setFont(AppFonts::codeFont(font().pointSizeF()));
    AppFonts::setInterfaceFont(title_, titleScale, QFont::Normal);
    auto *note = wrappedLabel(this);
    note->setText(tr("A separate %1 session reviews the code: it does not see your conversation and may only read "
                     "the project.")
                      .arg(session_->reviewerName()));
    note->setObjectName(QStringLiteral("reviewMuted"));
    error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(ReviewText::errorColor(palette())));

    clock_->setInterval(std::chrono::seconds(1));
    connect(clock_, &QTimer::timeout, this, &ReviewProgressView::updateElapsed);
    connect(session_, &ReviewSession::stateChanged, this, &ReviewProgressView::onStateChanged);
    connect(session_, &ReviewSession::stepsAdded, this, &ReviewProgressView::showNewSteps);

    auto *header = new QHBoxLayout;
    header->addWidget(kind_);
    header->addStretch();
    header->addWidget(elapsedLabel_);

    column_ = new QWidget(this);
    column_->setMaximumWidth(contentWidthPx);
    auto *layout = new QVBoxLayout(column_);
    layout->setSpacing(spacingPx);
    layout->addLayout(header);
    layout->addWidget(title_);
    layout->addSpacing(sectionSpacingPx);
    auto *reading = new QFrame(column_);
    reading_ = reading;
    reading->setObjectName(QStringLiteral("reviewReadingCard"));
    readingLayout_ = new QBoxLayout(QBoxLayout::LeftToRight, reading);
    readingLayout_->setContentsMargins(22, 22, 22, 22);
    readingLayout_->setSpacing(24);
    sketch_->setFixedSize(210, 190);
    readingLayout_->addWidget(sketch_, 0, Qt::AlignCenter);
    readingLayout_->addWidget(steps_, 1);
    layout->addWidget(reading);
    layout->addSpacing(spacingPx);
    layout->addWidget(note);
    layout->addWidget(error_);
    layout->addSpacing(spacingPx);
    layout->addLayout(createButtons());

    auto *outer = new QVBoxLayout(this);
    outer->addStretch(2);
    auto *row = new QHBoxLayout;
    row->addWidget(column_);
    outer->addLayout(row);
    outer->addStretch(3);
    onStateChanged(session_->state());
}

QLayout *ReviewProgressView::createButtons()
{
    connect(cancel_, &QPushButton::clicked, session_, &ReviewSession::interrupt);
    connect(retry_, &QPushButton::clicked, this, &ReviewProgressView::reviewRequested);
    connect(close_, &QPushButton::clicked, this, &ReviewProgressView::closeRequested);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel_);
    buttons->addWidget(retry_);
    buttons->addWidget(close_);
    return buttons;
}

void ReviewProgressView::onStateChanged(ReviewSession::State state)
{
    updateElapsed();
    switch (state) {
    case ReviewSession::State::Running:
        showRunning();
        return;
    case ReviewSession::State::Failed:
        showFailure();
        return;
    case ReviewSession::State::Interrupted:
        showInterrupted();
        return;
    case ReviewSession::State::CheckFailed:
        // The last step is the check.
        clock_->stop();
        sketch_->setState(ReviewSketch::State::Done);
        steps_->setState(StepTimeline::State::Failed);
        return;
    case ReviewSession::State::Finished:
        clock_->stop();
        sketch_->setState(ReviewSketch::State::Done);
        steps_->setState(StepTimeline::State::Done);
        return;
    }
}

void ReviewProgressView::showRunning()
{
    kind_->setText(tr("Review in progress").toUpper());
    reading_->show();
    title_->setText(tr("%1 is reviewing %2").arg(session_->agentName(), session_->subject()));
    error_->hide();
    retry_->hide();
    close_->hide();
    cancel_->show();
    sketch_->setState(ReviewSketch::State::Reading);
    // From the first step: a new start forgot the steps, and after a failing
    // check the review goes on below the check.
    steps_->clear();
    shownStepCount_ = 0;
    showNewSteps();
    clock_->start();
}

void ReviewProgressView::showFailure()
{
    kind_->setText(tr("Review failed").toUpper());
    reading_->hide();
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(ReviewText::errorColor(palette())));
    clock_->stop();
    sketch_->setState(ReviewSketch::State::Failed);
    steps_->setState(StepTimeline::State::Failed);
    title_->setText(tr("%1 could not review %2").arg(session_->agentName(), session_->subject()));
    error_->setText(session_->error());
    error_->show();
    cancel_->hide();
    retry_->show();
    close_->show();
}

void ReviewProgressView::showInterrupted()
{
    clock_->stop();
    sketch_->setState(ReviewSketch::State::Done);
    steps_->setState(StepTimeline::State::Done);
    reading_->hide();
    kind_->setText(tr("Review interrupted").toUpper());
    title_->setText(tr("The review did not finish."));
    error_->setText(tr("No complete report is available. Your files are unchanged."));
    error_->setStyleSheet(QString());
    error_->setObjectName(QStringLiteral("reviewMuted"));
    error_->show();
    cancel_->hide();
    retry_->show();
    close_->show();
}

void ReviewProgressView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    readingLayout_->setDirection(width() < 650 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
}

void ReviewProgressView::showNewSteps()
{
    const QStringList &steps = session_->steps();
    steps_->addSteps(steps.mid(shownStepCount_));
    shownStepCount_ = steps.size();
}

void ReviewProgressView::updateElapsed()
{
    elapsedLabel_->setText(ReviewText::elapsed(session_->elapsed()));
}
