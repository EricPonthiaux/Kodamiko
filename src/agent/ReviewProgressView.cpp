// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewProgressView.h"

#include "ReviewSketch.h"
#include "ReviewText.h"
#include "ui/AppFonts.h"
#include "ui/StepTimeline.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// As the tab of a diagram being drawn (DiagramGenerationView).
constexpr int contentWidthPx = 560;
constexpr qreal kindScale = 0.85;
constexpr qreal kindLetterSpacingPx = 1.2;
constexpr qreal titleScale = 1.3;
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
    , cancel_(new QPushButton(tr("Cancel"), this))
    , retry_(new QPushButton(tr("Review Again"), this))
    , close_(new QPushButton(tr("Close"), this))
{
    auto *kind = new QLabel(tr("Review").toUpper(), this);
    kind->setForegroundRole(QPalette::Highlight);
    AppFonts::setInterfaceFont(kind, kindScale, QFont::Bold, kindLetterSpacingPx);
    elapsedLabel_->setForegroundRole(QPalette::PlaceholderText);
    elapsedLabel_->setFont(AppFonts::codeFont(font().pointSizeF()));
    AppFonts::setInterfaceFont(title_, titleScale);
    auto *note = wrappedLabel(this);
    note->setText(tr("A separate %1 session reviews the code: it does not see your conversation and may only read "
                     "the project.")
                      .arg(session_->reviewerName()));
    note->setForegroundRole(QPalette::PlaceholderText);
    error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(ReviewText::errorColor(palette())));

    clock_->setInterval(std::chrono::seconds(1));
    connect(clock_, &QTimer::timeout, this, &ReviewProgressView::updateElapsed);
    connect(session_, &ReviewSession::stateChanged, this, &ReviewProgressView::onStateChanged);
    connect(session_, &ReviewSession::stepsAdded, this, &ReviewProgressView::showNewSteps);

    auto *header = new QHBoxLayout;
    header->addWidget(kind);
    header->addStretch();
    header->addWidget(elapsedLabel_);

    auto *column = new QWidget(this);
    // Wrapped labels need a width to compute their height.
    column->setFixedWidth(contentWidthPx);
    auto *layout = new QVBoxLayout(column);
    layout->setSpacing(spacingPx);
    layout->addWidget(sketch_);
    layout->addSpacing(sectionSpacingPx);
    layout->addLayout(header);
    layout->addWidget(title_);
    layout->addWidget(note);
    layout->addSpacing(sectionSpacingPx);
    layout->addWidget(steps_);
    layout->addWidget(error_);
    layout->addSpacing(spacingPx);
    layout->addLayout(createButtons());

    auto *outer = new QVBoxLayout(this);
    outer->addStretch(2);
    outer->addWidget(column, 0, Qt::AlignHCenter);
    outer->addStretch(3);
    onStateChanged(session_->state());
}

QLayout *ReviewProgressView::createButtons()
{
    connect(cancel_, &QPushButton::clicked, this, [this] {
        session_->stop();
        emit closeRequested();
    });
    connect(retry_, &QPushButton::clicked, session_, &ReviewSession::start);
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
