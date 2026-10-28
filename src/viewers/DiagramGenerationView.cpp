// SPDX-License-Identifier: GPL-3.0-only

#include "DiagramGenerationView.h"

#include "DiagramCheck.h"
#include "StepTimeline.h"
#include "agent/AgentProvider.h"
#include "agent/AgentRun.h"
#include "agent/DiagramRequest.h"
#include "ui/AppFonts.h"
#include "viewers/sketch/DiagramSketch.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace {

// The agent may read a good part of the project first.
constexpr std::chrono::minutes timeout{10};
constexpr int contentWidth = 560;
constexpr qreal kindScale = 0.85;
constexpr qreal kindLetterSpacingPx = 1.2;

QString errorColor(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128 ? QStringLiteral("#e06c75") : QStringLiteral("#e45649");
}

// Why the diagram is missing: the agent's failure, its last answer, or its
// error output, the most telling first.
QString failureMessage(const QString &agentName, const QString &answer, bool answerFailed, const QString &errorOutput)
{
    if (answerFailed && !answer.isEmpty())
        return answer;
    if (!answer.isEmpty())
        return DiagramGenerationView::tr("%1 did not write the diagram: %2").arg(agentName, answer);
    if (!errorOutput.isEmpty())
        return errorOutput;
    return DiagramGenerationView::tr("%1 did not write the diagram.").arg(agentName);
}

} // namespace

DiagramGenerationView::DiagramGenerationView(Request drawing, QWidget *parent)
    : QWidget(parent)
    , workDir_(std::move(drawing.workDir))
    , path_(std::move(drawing.path))
    , kindTitle_(std::move(drawing.kindTitle))
    , description_(std::move(drawing.description))
    , agent_(drawing.agent)
    , run_(new AgentRun(agent_, workDir_, this))
    , clock_(new QTimer(this))
    , sketch_(new DiagramSketch(drawing.kindId, this))
    , title_(new QLabel(this))
    , steps_(new StepTimeline(this))
    , elapsedLabel_(new QLabel(this))
    , error_(new QLabel(this))
    , cancel_(new QPushButton(tr("Cancel"), this))
    , retry_(new QPushButton(tr("Try Again"), this))
    , close_(new QPushButton(tr("Close"), this))
{
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);

    auto *kind = new QLabel(kindTitle_.toUpper(), this);
    kind->setForegroundRole(QPalette::Highlight);
    AppFonts::setInterfaceFont(kind, kindScale, QFont::Bold, kindLetterSpacingPx);
    elapsedLabel_->setForegroundRole(QPalette::PlaceholderText);
    elapsedLabel_->setFont(AppFonts::codeFont(font().pointSizeF()));

    title_->setWordWrap(true);
    AppFonts::setInterfaceFont(title_, 1.3);

    error_->setWordWrap(true);
    error_->setTextFormat(Qt::PlainText);
    error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(errorColor(palette())));

    connect(cancel_, &QPushButton::clicked, this, [this] {
        stop();
        emit closeRequested();
    });
    connect(close_, &QPushButton::clicked, this, &DiagramGenerationView::closeRequested);
    connect(retry_, &QPushButton::clicked, this, &DiagramGenerationView::start);
    connect(clock_, &QTimer::timeout, this, &DiagramGenerationView::updateElapsed);
    clock_->setInterval(1000);
    Q_ASSERT(agent_);
    hookServer_.setProvider(agent_);
    connect(run_, &AgentRun::stepsAdded, steps_, &StepTimeline::addSteps);
    connect(run_, &AgentRun::finished, this, &DiagramGenerationView::onRunFinished);
    connect(run_, &AgentRun::failed, this, &DiagramGenerationView::fail);
    connect(&hookServer_, &HookServer::hookReceived, this, &DiagramGenerationView::onHookReceived,
            Qt::DirectConnection);

    auto *header = new QHBoxLayout;
    header->addWidget(kind);
    header->addStretch();
    header->addWidget(elapsedLabel_);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel_);
    buttons->addWidget(retry_);
    buttons->addWidget(close_);

    auto *column = new QWidget(this);
    // Wrapped labels need a width to compute their height.
    column->setFixedWidth(contentWidth);
    auto *layout = new QVBoxLayout(column);
    layout->setSpacing(8);
    layout->addWidget(sketch_);
    layout->addSpacing(16);
    layout->addLayout(header);
    layout->addWidget(title_);
    layout->addSpacing(16);
    layout->addWidget(steps_);
    layout->addWidget(error_);
    layout->addSpacing(8);
    layout->addLayout(buttons);

    auto *outer = new QVBoxLayout(this);
    outer->addStretch(2);
    outer->addWidget(column, 0, Qt::AlignHCenter);
    outer->addStretch(3);
}

DiagramGenerationView::~DiagramGenerationView()
{
    stop();
}

void DiagramGenerationView::start()
{
    stop();
    steps_->clear();
    title_->setText(
        tr("%1 is drawing %2").arg(agent_->name(), QDir::toNativeSeparators(QDir(workDir_).relativeFilePath(path_))));
    error_->hide();
    retry_->hide();
    close_->hide();
    cancel_->show();
    sketch_->setState(DiagramSketch::State::Drawing);
    steps_->addSteps({tr("Starting %1").arg(agent_->name())});

    // duet-hook reaches this view's own server: the agent gets the diagram's
    // problems right after writing it.
    if (hookServer_.serverName().isEmpty())
        hookServer_.listen();
    const QString relative = QDir(workDir_).relativeFilePath(path_);
    QString launchError;
    const std::optional<ProcessSpec> spec =
        agent_->fileRunProcess({.workingDirectory = workDir_,
                                .prompt = DiagramRequest::prompt(kindTitle_, relative, description_),
                                .relativePath = relative,
                                .hookServerName = hookServer_.serverName()},
                               &launchError);
    if (!spec) {
        fail(launchError);
        return;
    }

    // The clock first: a run that cannot start fails inside start(), and
    // fail() stops it.
    elapsed_.start();
    updateElapsed();
    clock_->start();
    run_->start(*spec, timeout);
}

void DiagramGenerationView::stop()
{
    clock_->stop();
    run_->stop();
}

void DiagramGenerationView::onRunFinished(const AgentRun::Outcome &outcome)
{
    clock_->stop();
    if (!outcome.failed && QFileInfo::exists(path_)) {
        sketch_->setState(DiagramSketch::State::Done);
        emit finished(path_, outcome.answer);
        return;
    }
    fail(failureMessage(agent_->name(), outcome.answer, outcome.failed, outcome.errorOutput));
}

void DiagramGenerationView::onHookReceived(const HookEvent &event)
{
    if (event.kind != HookEvent::Kind::FilesEdited)
        return;
    for (const QString &path : event.filePaths) {
        if (QFileInfo(path).absoluteFilePath() != QFileInfo(path_).absoluteFilePath())
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QStringList problems = DiagramCheck::problems(path, file.readAll());
        if (!problems.isEmpty()) {
            hookServer_.respond(
                agent_->blockReply(DiagramCheck::feedback(QDir(workDir_).relativeFilePath(path), problems)));
            steps_->addSteps({tr("Fixing what the diagram editor could not read")});
        }
    }
}

void DiagramGenerationView::fail(const QString &error)
{
    stop();
    sketch_->setState(DiagramSketch::State::Failed);
    steps_->setState(StepTimeline::State::Failed);
    title_->setText(tr("%1 could not draw the diagram").arg(agent_->name()));
    error_->setText(error);
    error_->show();
    cancel_->hide();
    retry_->show();
    close_->show();
}

void DiagramGenerationView::updateElapsed()
{
    const qint64 seconds = elapsed_.elapsed() / 1000;
    elapsedLabel_->setText(QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')));
}
