#include "CommitMessage.h"

#include "ClaudeLauncher.h"
#include "git/FileChange.h"
#include "git/GitProcess.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace CommitMessage {
namespace {

constexpr int timeoutMs = 120'000;

const QStringList diffOptions{QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff")};

QString text(const QByteArray &bytes)
{
    return QString::fromUtf8(bytes).trimmed();
}

// The tree to diff the index against when amending: the parent of HEAD, or
// the empty tree when HEAD is the first commit.
QString amendBase(const QString &workDir)
{
    try {
        return text(Git::run(workDir, {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                                       QStringLiteral("--quiet"), QStringLiteral("HEAD^")}));
    } catch (const GitError &) {
        return text(Git::run(workDir, {QStringLiteral("mktree")}));
    }
}

// Wraps the body's long lines at 72 columns, as git expects; a list item's
// continuation lines are indented under its text. The summary line and
// indented lines (code) are kept as they are.
QString wrapBody(const QString &message)
{
    constexpr int width = 72;
    static const QRegularExpression item(QStringLiteral("^([-*]|\\d+[.)]) "));
    QStringList lines = message.split(QLatin1Char('\n'));
    QStringList wrapped;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        QString line = lines[i];
        if (i == 0 || line.size() <= width || line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'))) {
            wrapped.append(line);
            continue;
        }
        const QRegularExpressionMatch match = item.match(line);
        const QString indent(match.hasMatch() ? match.capturedLength() : 0, QLatin1Char(' '));
        QString current;
        for (const QString &word : line.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (!current.isEmpty() && current.size() + 1 + word.size() > width) {
                wrapped.append(current);
                current = indent;
            } else if (!current.isEmpty() && current != indent) {
                current += QLatin1Char(' ');
            }
            current += word;
        }
        wrapped.append(current);
    }
    return wrapped.join(QLatin1Char('\n'));
}

} // namespace

Changes read(const RepositoryInfo &repository, bool amend, bool stageEverything, const QString &pathspec)
{
    const QString &workDir = repository.workDir;
    Changes changes;
    if (!repository.unborn) {
        const QString log = QString::fromUtf8(Git::run(workDir, {QStringLiteral("log"), QStringLiteral("-n"),
                                                                 QStringLiteral("15"), QStringLiteral("--format=%s")}));
        changes.recentSubjects = log.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    QStringList range;
    QStringList paths;
    if (stageEverything && !amend) {
        // "git diff HEAD" also covers what is already staged.
        if (!repository.unborn)
            range = {QStringLiteral("HEAD")};
        paths = {QStringLiteral("--"), pathspec.isEmpty() ? QStringLiteral(".") : pathspec};
        changes.untracked = Git::splitNul(
            Git::run(workDir, QStringList{QStringLiteral("ls-files"), QStringLiteral("-z"), QStringLiteral("--others"),
                                          QStringLiteral("--exclude-standard")} +
                                  paths));
    } else {
        range = {QStringLiteral("--cached")};
        if (amend)
            range.append(amendBase(workDir));
    }
    const QStringList diff = QStringList{QStringLiteral("diff")} + diffOptions + range;
    changes.stat = Git::run(workDir, diff + QStringList{QStringLiteral("--stat")} + paths);
    changes.diff = Git::run(workDir, diff + paths);
    return changes;
}

QString systemPrompt()
{
    // On one line: cmd.exe, which launches claude.cmd, would cut it at a line break.
    return QStringLiteral(
        "You write git commit messages. The standard input holds the subjects of the repository's recent "
        "commits, then the changes to commit. Reply with the commit message only: no preamble, no code "
        "fences, no quotes, no trailers such as Co-Authored-By. "
        "First line: a summary of the change in the imperative mood, at most 72 characters, without a final "
        "period, following the style and the language of the recent subjects (ignore placeholder subjects "
        "such as '++', 'wip' or 'next'). "
        "Then, only if the change is not obvious from the summary, a blank line and a short body wrapped at "
        "72 columns that explains why the change is made, not how.");
}

QByteArray input(const Changes &changes, int diffLimit)
{
    QByteArray input;
    if (!changes.recentSubjects.isEmpty())
        input += "Recent commits:\n" + changes.recentSubjects.join(QLatin1Char('\n')).toUtf8() + "\n\n";
    input += "Files changed:\n" + changes.stat + '\n';
    if (!changes.untracked.isEmpty())
        input += "New files:\n" + changes.untracked.join(QLatin1Char('\n')).toUtf8() + "\n\n";
    input += "Diff:\n";
    if (changes.diff.size() > diffLimit)
        input += changes.diff.left(diffLimit) + "\n[diff truncated]\n";
    else
        input += changes.diff;
    return input;
}

std::optional<QString> parseReply(const QByteArray &json, QString *error)
{
    const QJsonObject reply = QJsonDocument::fromJson(json).object();
    if (reply.isEmpty()) {
        *error = QObject::tr("Claude replied in an unexpected way.");
        return std::nullopt;
    }
    QString message = reply.value(QLatin1String("result")).toString().trimmed();
    if (reply.value(QLatin1String("is_error")).toBool()) {
        *error = message.isEmpty() ? QObject::tr("Claude stopped on an error.") : message;
        return std::nullopt;
    }

    static const QRegularExpression fence(QStringLiteral("^```[\\w-]*\\n(.*)\\n```$"),
                                          QRegularExpression::DotMatchesEverythingOption);
    if (const QRegularExpressionMatch match = fence.match(message); match.hasMatch())
        message = match.captured(1).trimmed();
    if (message.size() >= 2 && message.startsWith(QLatin1Char('"')) && message.endsWith(QLatin1Char('"')))
        message = message.mid(1, message.size() - 2).trimmed();
    if (message.isEmpty()) {
        *error = QObject::tr("Claude replied with an empty message.");
        return std::nullopt;
    }
    return wrapBody(message);
}

} // namespace CommitMessage

CommitMessageGenerator::CommitMessageGenerator(QObject *parent)
    : QObject(parent)
    , timeout_(new QTimer(this))
{
    pool_.setMaxThreadCount(1);
    timeout_->setSingleShot(true);
    timeout_->setInterval(CommitMessage::timeoutMs);
    connect(timeout_, &QTimer::timeout, this, [this] {
        cancel();
        emit failed(tr("Claude took too long to reply."));
    });
}

CommitMessageGenerator::~CommitMessageGenerator()
{
    cancel();
    pool_.waitForDone();
}

void CommitMessageGenerator::start(const RepositoryInfo &repository,
                                   bool amend,
                                   bool stageEverything,
                                   const QString &pathspec)
{
    cancel();
    setRunning(true);
    timeout_->start();
    pool_.start([this, repository, amend, stageEverything, pathspec, generation = generation_] {
        std::optional<CommitMessage::Changes> changes;
        QString error;
        try {
            changes = CommitMessage::read(repository, amend, stageEverything, pathspec);
        } catch (const GitError &exception) {
            error = QString::fromStdString(exception.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, changes, error, workDir = repository.workDir, generation] {
                onChangesRead(changes, error, workDir, generation);
            },
            Qt::QueuedConnection);
    });
}

void CommitMessageGenerator::cancel()
{
    ++generation_;
    timeout_->stop();
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(1000);
        process_->deleteLater();
        process_ = nullptr;
    }
    setRunning(false);
}

void CommitMessageGenerator::onChangesRead(const std::optional<CommitMessage::Changes> &changes,
                                           const QString &error,
                                           const QString &workDir,
                                           int generation)
{
    if (generation != generation_)
        return;
    if (!changes) {
        fail(error);
        return;
    }
    if (changes->diff.trimmed().isEmpty() && changes->untracked.isEmpty()) {
        fail(tr("Nothing to commit"));
        return;
    }

    QString launchError;
    const std::optional<ProcessSpec> spec = ClaudeLauncher::printProcess(
        workDir,
        {QStringLiteral("--model"), QStringLiteral("haiku"), QStringLiteral("--tools"), QString(),
         QStringLiteral("--no-session-persistence"), QStringLiteral("--output-format"), QStringLiteral("json"),
         QStringLiteral("--system-prompt"), CommitMessage::systemPrompt(),
         QStringLiteral("Write the commit message for these changes.")},
        &launchError);
    if (!spec) {
        fail(launchError);
        return;
    }

    process_ = new QProcess(this);
    process_->setProgram(spec->program);
    process_->setArguments(spec->arguments);
    process_->setWorkingDirectory(spec->workingDirectory);
    process_->setProcessEnvironment(spec->environment);
    connect(process_, &QProcess::finished, this, [this, generation] { onProcessFinished(generation); });
    connect(process_, &QProcess::errorOccurred, this, [this, generation](QProcess::ProcessError processError) {
        if (processError == QProcess::FailedToStart && generation == generation_)
            fail(tr("Could not start Claude: %1").arg(process_->errorString()));
    });
    process_->start();
    process_->write(CommitMessage::input(*changes));
    process_->closeWriteChannel();
}

void CommitMessageGenerator::onProcessFinished(int generation)
{
    if (generation != generation_ || !process_)
        return;
    const QByteArray output = process_->readAllStandardOutput();
    const QString errorOutput = QString::fromUtf8(process_->readAllStandardError()).trimmed();
    QString error;
    const std::optional<QString> message = CommitMessage::parseReply(output, &error);
    if (!message) {
        // Without JSON, `claude` explains itself on its error output.
        fail(output.trimmed().isEmpty() && !errorOutput.isEmpty() ? errorOutput : error);
        return;
    }
    cancel();
    emit finished(*message);
}

void CommitMessageGenerator::fail(const QString &error)
{
    cancel();
    emit failed(error);
}

void CommitMessageGenerator::setRunning(bool running)
{
    if (running == running_)
        return;
    running_ = running;
    emit runningChanged(running);
}
