#include "DiagramGenerationView.h"

#include "DiagramCheck.h"
#include "claude/ClaudeLauncher.h"
#include "claude/DiagramRequest.h"
#include "ui/AppFonts.h"
#include "ui/Motion.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace {

// Claude may read a good part of the project first.
constexpr int timeoutMs = 10 * 60 * 1000;
constexpr int shownSteps = 6;
constexpr int contentWidth = 520;

// A turning arc in the accent color.
class Spinner : public QWidget {
public:
    explicit Spinner(QWidget *parent)
        : QWidget(parent)
        , timer_(new QTimer(this))
    {
        setFixedSize(40, 40);
        timer_->setInterval(16);
        connect(timer_, &QTimer::timeout, this, [this] {
            angle_ = (angle_ + 6) % 360;
            update();
        });
        connect(&Motion::policy(), &Motion::Policy::enabledChanged, this, [this] { updateMotion(); });
    }

    void setSpinning(bool spinning)
    {
        spinning_ = spinning;
        updateMotion();
        setVisible(spinning);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF area = QRectF(rect()).adjusted(4, 4, -4, -4);
        QColor track = palette().color(QPalette::PlaceholderText);
        track.setAlphaF(0.25);
        painter.setPen(QPen(track, 3));
        painter.drawEllipse(area);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 3, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(area, -angle_ * 16, 100 * 16);
    }

private:
    void updateMotion()
    {
        if (spinning_ && Motion::policy().isEnabled())
            timer_->start();
        else
            timer_->stop();
        update();
    }

    QTimer *timer_;
    int angle_ = 0;
    bool spinning_ = false;
};

QString errorColor(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128 ? QStringLiteral("#e06c75") : QStringLiteral("#e45649");
}

// Why the diagram is missing: Claude's failure, its last answer, or its
// error output, the most telling first.
QString failureMessage(const QString &answer, bool answerFailed, const QString &errorOutput)
{
    if (answerFailed && !answer.isEmpty())
        return answer;
    if (!answer.isEmpty())
        return DiagramGenerationView::tr("Claude did not write the diagram: %1").arg(answer);
    if (!errorOutput.isEmpty())
        return errorOutput;
    return DiagramGenerationView::tr("Claude did not write the diagram.");
}

} // namespace

DiagramGenerationView::DiagramGenerationView(Request drawing, QWidget *parent)
    : QWidget(parent)
    , workDir_(std::move(drawing.workDir))
    , path_(std::move(drawing.path))
    , kindTitle_(std::move(drawing.kindTitle))
    , description_(std::move(drawing.description))
    , clock_(new QTimer(this))
    , spinner_(new Spinner(this))
    , title_(new QLabel(this))
    , stepsLabel_(new QLabel(this))
    , elapsedLabel_(new QLabel(this))
    , error_(new QLabel(this))
    , cancel_(new QPushButton(tr("Cancel"), this))
    , retry_(new QPushButton(tr("Try Again"), this))
    , close_(new QPushButton(tr("Close"), this))
{
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);

    title_->setWordWrap(true);
    title_->setAlignment(Qt::AlignCenter);
    AppFonts::setInterfaceFont(title_, 1.3);

    auto *request = new QLabel(description_.trimmed(), this);
    request->setWordWrap(true);
    request->setAlignment(Qt::AlignCenter);
    request->setTextFormat(Qt::PlainText);
    request->setForegroundRole(QPalette::PlaceholderText);

    stepsLabel_->setAlignment(Qt::AlignCenter);
    stepsLabel_->setTextFormat(Qt::RichText);
    stepsLabel_->setWordWrap(true);
    elapsedLabel_->setAlignment(Qt::AlignCenter);
    elapsedLabel_->setForegroundRole(QPalette::PlaceholderText);
    error_->setWordWrap(true);
    error_->setAlignment(Qt::AlignCenter);
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
    connect(&hookServer_, &HookServer::hookReceived, this, &DiagramGenerationView::onHookReceived,
            Qt::DirectConnection);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel_);
    buttons->addWidget(retry_);
    buttons->addWidget(close_);
    buttons->addStretch();

    auto *column = new QWidget(this);
    // Wrapped labels need a width to compute their height.
    column->setFixedWidth(contentWidth);
    auto *layout = new QVBoxLayout(column);
    layout->setSpacing(12);
    layout->addWidget(spinner_, 0, Qt::AlignHCenter);
    layout->addWidget(title_);
    layout->addWidget(request);
    layout->addSpacing(8);
    layout->addWidget(stepsLabel_);
    layout->addWidget(elapsedLabel_);
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
    steps_.clear();
    answer_.clear();
    answerFailed_ = false;
    pending_.clear();
    title_->setText(tr("Claude is drawing %1").arg(QDir::toNativeSeparators(QDir(workDir_).relativeFilePath(path_))));
    error_->hide();
    retry_->hide();
    close_->hide();
    cancel_->show();
    static_cast<Spinner *>(spinner_)->setSpinning(true);
    addSteps({tr("Starting Claude")});

    // duet-hook reaches this view's own server: Claude gets the diagram's
    // problems right after writing it.
    if (hookServer_.serverName().isEmpty())
        hookServer_.listen();
    const QString hook = hookServer_.serverName().isEmpty() ? QString() : ClaudeLauncher::hookCommand();

    const QString relative = QDir(workDir_).relativeFilePath(path_);
    QString launchError;
    std::optional<ProcessSpec> spec = ClaudeLauncher::printProcess(
        workDir_,
        DiagramRequest::arguments(DiagramRequest::prompt(kindTitle_, relative, description_), relative,
                                  ClaudeLauncher::pluginPath(), hook),
        &launchError);
    if (!spec) {
        fail(launchError);
        return;
    }
    if (!hook.isEmpty())
        spec->environment.insert(QStringLiteral("DUET_SOCKET"), hookServer_.serverName());

    process_ = new QProcess(this);
    process_->setProgram(spec->program);
    process_->setArguments(spec->arguments);
    process_->setWorkingDirectory(spec->workingDirectory);
    process_->setProcessEnvironment(spec->environment);
    connect(process_, &QProcess::readyReadStandardOutput, this, &DiagramGenerationView::readOutput);
    connect(process_, &QProcess::finished, this, &DiagramGenerationView::onProcessFinished);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError processError) {
        if (processError == QProcess::FailedToStart)
            fail(tr("Could not start Claude: %1").arg(process_->errorString()));
    });
    process_->start();
    process_->closeWriteChannel();
    elapsed_.start();
    updateElapsed();
    clock_->start();
    QTimer::singleShot(timeoutMs, process_, [this] { fail(tr("Claude took too long to draw the diagram.")); });
}

void DiagramGenerationView::stop()
{
    clock_->stop();
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(1000);
        process_->deleteLater();
        process_ = nullptr;
    }
}

void DiagramGenerationView::readOutput()
{
    pending_ += process_->readAllStandardOutput();
    qsizetype newline = 0;
    while ((newline = pending_.indexOf('\n')) >= 0) {
        const QByteArray line = pending_.left(newline);
        pending_.remove(0, newline + 1);
        const DiagramRequest::Event event = DiagramRequest::parseLine(line, workDir_);
        if (event.kind == DiagramRequest::Event::Kind::Steps) {
            addSteps(event.steps);
        } else if (event.kind == DiagramRequest::Event::Kind::Finished) {
            answer_ = event.text;
            answerFailed_ = event.failed;
        }
    }
}

void DiagramGenerationView::onProcessFinished()
{
    if (!process_)
        return;
    readOutput();
    const QString errorOutput = QString::fromUtf8(process_->readAllStandardError()).trimmed();
    stop();
    if (!answerFailed_ && QFileInfo::exists(path_)) {
        emit finished(path_, answer_);
        return;
    }
    fail(failureMessage(answer_, answerFailed_, errorOutput));
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
            hookServer_.respond(DiagramCheck::feedbackOutput(QDir(workDir_).relativeFilePath(path), problems));
            addSteps({tr("Fixing what the diagram editor could not read")});
        }
    }
}

void DiagramGenerationView::addSteps(const QStringList &steps)
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

void DiagramGenerationView::fail(const QString &error)
{
    stop();
    static_cast<Spinner *>(spinner_)->setSpinning(false);
    title_->setText(tr("Claude could not draw the diagram"));
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
