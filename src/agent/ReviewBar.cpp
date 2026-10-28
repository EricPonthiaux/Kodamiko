// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewBar.h"

#include "ReviewText.h"
#include "ui/AppFonts.h"
#include "ui/Spinner.h"
#include "ui/ThemeStyle.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// Lines of a failing check's output shown.
constexpr int shownOutputLines = 200;
// The output's height, in lines of text: the diff keeps the rest.
constexpr int outputLines = 10;

} // namespace

ReviewBar::ReviewBar(QWidget *parent)
    : QFrame(parent)
    , clock_(new QTimer(this))
    , spinner_(new Spinner(this))
    , title_(new QLabel(this))
    , step_(new QLabel(this))
    , elapsedLabel_(new QLabel(this))
    , message_(new QLabel(this))
    , output_(new QTextBrowser(this))
    , findings_(new QPushButton(this))
    , cancel_(new QPushButton(tr("Cancel"), this))
    , reviewAnyway_(new QPushButton(tr("Review Anyway"), this))
    , retry_(new QPushButton(tr("Review Again"), this))
    , sendAll_(new QPushButton(this))
    , close_(new QPushButton(tr("Close"), this))
{
    setObjectName(QStringLiteral("card"));
    title_->setTextFormat(Qt::PlainText);
    AppFonts::setInterfaceFont(title_, 1.05);
    // Elided by the layout rather than widening the bar.
    step_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    step_->setTextFormat(Qt::PlainText);
    step_->setForegroundRole(QPalette::PlaceholderText);
    elapsedLabel_->setForegroundRole(QPalette::PlaceholderText);
    message_->setWordWrap(true);
    message_->setTextFormat(Qt::PlainText);
    message_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    output_->setFrameShape(QFrame::NoFrame);
    output_->setOpenLinks(false);
    output_->setFixedHeight(outputLines * fontMetrics().lineSpacing());

    connect(clock_, &QTimer::timeout, this, &ReviewBar::updateElapsed);
    clock_->setInterval(1000);

    auto *top = new QHBoxLayout;
    top->setSpacing(8);
    top->addWidget(spinner_);
    top->addWidget(title_);
    top->addWidget(step_, 1);
    top->addWidget(elapsedLabel_);
    top->addLayout(createButtons());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 8, 10, 8);
    layout->setSpacing(6);
    layout->addLayout(top);
    layout->addWidget(message_);
    layout->addWidget(output_);
    hide();
}

QLayout *ReviewBar::createButtons()
{
    findings_->setToolTip(tr("Show the findings over the diff"));
    connect(findings_, &QPushButton::clicked, this, &ReviewBar::findingsRequested);
    reviewAnyway_->setToolTip(tr("Review the code although the check failed, telling the reviewer the failure"));
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (session_)
            session_->stop();
        emit closeRequested();
    });
    connect(reviewAnyway_, &QPushButton::clicked, this, [this] {
        if (session_)
            session_->reviewAnyway();
    });
    connect(retry_, &QPushButton::clicked, this, &ReviewBar::reviewAgainRequested);
    connect(sendAll_, &QPushButton::clicked, this, [this] {
        if (session_)
            session_->sendAll();
    });
    connect(close_, &QPushButton::clicked, this, &ReviewBar::closeRequested);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    for (QWidget *button :
         std::initializer_list<QWidget *>{findings_, cancel_, reviewAnyway_, retry_, sendAll_, close_})
        buttons->addWidget(button);
    return buttons;
}

void ReviewBar::setSession(ReviewSession *session)
{
    if (session_)
        session_->disconnect(this);
    session_ = session;
    setVisible(session);
    if (!session) {
        clock_->stop();
        return;
    }
    sendAll_->setText(tr("Send All to %1").arg(session->agentName()));
    sendAll_->setToolTip(
        tr("Type every finding left in %1's prompt, for you to complete and send").arg(session->agentName()));
    connect(session, &ReviewSession::stateChanged, this, &ReviewBar::onStateChanged);
    connect(session, &ReviewSession::stepsAdded, this, &ReviewBar::showLastStep);
    connect(session, &ReviewSession::findingsChanged, this, &ReviewBar::updateFindings);
    onStateChanged(session->state());
}

void ReviewBar::onStateChanged(ReviewSession::State state)
{
    for (QWidget *widget : std::initializer_list<QWidget *>{step_, message_, output_, findings_, cancel_, reviewAnyway_,
                                                            retry_, sendAll_, close_})
        widget->hide();
    updateElapsed();
    const bool isRunning = state == ReviewSession::State::Running;
    spinner_->setSpinning(isRunning);
    if (isRunning)
        clock_->start();
    else
        clock_->stop();
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

void ReviewBar::showProgress()
{
    setText(tr("%1 is reviewing %2").arg(session_->reviewerName(), session_->subject()), {});
    showLastStep();
    step_->show();
    cancel_->show();
}

void ReviewBar::showCheckFailure()
{
    if (!session_->failedCheck())
        return;
    const ReviewRequest::Check &check = *session_->failedCheck();
    setText(tr("The project's check failed"), ReviewText::checkFailure(check), ReviewText::errorColor(palette()));
    output_->setFont(AppFonts::codeFont(font().pointSizeF()));
    output_->setPlainText(check.result.lastLines(shownOutputLines));
    output_->show();
    for (QWidget *widget : std::initializer_list<QWidget *>{reviewAnyway_, retry_, close_})
        widget->show();
}

void ReviewBar::showFinished()
{
    retry_->show();
    close_->show();
    if (!session_->rawAnswer().isEmpty()) {
        setText(tr("Review of %1").arg(session_->subject()), {});
        output_->setFont(font());
        output_->setMarkdown(session_->rawAnswer());
        output_->show();
        sendAll_->show();
        return;
    }
    // Once: the findings dismissed later were found all the same. With
    // findings, the summary is over them (ReviewFindingsPanel).
    const QString summary = session_->findings().isEmpty() ? ReviewText::summary(*session_) : QString();
    setText(tr("Review of %1").arg(session_->subject()), summary);
    updateFindings();
}

void ReviewBar::showFailure()
{
    setText(tr("%1 could not review %2").arg(session_->agentName(), session_->subject()), session_->error(),
            ReviewText::errorColor(palette()));
    retry_->show();
    close_->show();
}

void ReviewBar::showLastStep()
{
    if (session_ && !session_->steps().isEmpty())
        step_->setText(session_->steps().constLast() + QStringLiteral("…"));
}

void ReviewBar::updateFindings()
{
    const qsizetype count = session_->findings().size();
    findings_->setText(count == 1 ? tr("1 Finding") : tr("%1 Findings").arg(count));
    findings_->setVisible(count > 0);
    sendAll_->setVisible(count > 0);
    sendAll_->setEnabled(!session_->notHandedOver(session_->findings()).isEmpty());
}

void ReviewBar::setText(const QString &title, const QString &message, const QString &color)
{
    title_->setText(title);
    message_->setText(message);
    ThemeStyle::setStyleSheetIfChanged(message_, color.isEmpty() ? QString() : QStringLiteral("color: %1;").arg(color));
    message_->setVisible(!message.isEmpty());
}

void ReviewBar::updateElapsed()
{
    if (session_)
        elapsedLabel_->setText(ReviewText::elapsed(session_->elapsed()));
}
