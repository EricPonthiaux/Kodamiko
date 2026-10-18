#include "GitProcess.h"

#include "FileChange.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Git {
namespace {

constexpr int pollMs = 100;

// git redraws its progress lines with "\r": the pieces of `buffer` ended by
// "\r" or "\n" are complete lines; the rest stays in the buffer.
QStringList takeLines(QByteArray &buffer)
{
    QStringList lines;
    qsizetype start = 0;
    for (qsizetype i = 0; i < buffer.size(); ++i) {
        if (buffer.at(i) == '\r' || buffer.at(i) == '\n') {
            const QString line = QString::fromUtf8(buffer.mid(start, i - start)).trimmed();
            if (!line.isEmpty())
                lines.append(line);
            start = i + 1;
        }
    }
    buffer.remove(0, start);
    return lines;
}

// git's error output without the progress lines printed before the error
// (asked for with --progress).
QString withoutProgress(const QByteArray &errorOutput)
{
    static const QRegularExpression progressLine(QStringLiteral(R"(^(remote: )?[\w ]+: +\d+% \()"));
    QStringList kept;
    for (const QString &line : QString::fromUtf8(errorOutput).split(QRegularExpression(QStringLiteral("[\r\n]")))) {
        if (!line.trimmed().isEmpty() && !progressLine.match(line).hasMatch())
            kept.append(line);
    }
    return kept.join(QLatin1Char('\n')).trimmed();
}

} // namespace

QByteArray run(const QString &workDir, const QStringList &arguments, const RunOptions &options)
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty())
        throw GitError(QObject::tr("The git command was not found: install git and make sure it is in the PATH."));

    QProcessEnvironment processEnvironment = QProcessEnvironment::systemEnvironment();
    processEnvironment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    processEnvironment.insert(QStringLiteral("GIT_LITERAL_PATHSPECS"), QStringLiteral("1"));
    // git recognizes ":" as an editor that does nothing.
    processEnvironment.insert(QStringLiteral("GIT_EDITOR"), QStringLiteral(":"));
    for (const QHash<QString, QString> *environment : {&options.environment, &options.feedback.environment}) {
        for (auto it = environment->begin(); it != environment->end(); ++it)
            processEnvironment.insert(it.key(), it.value());
    }

    QProcess process;
    process.setWorkingDirectory(workDir);
    process.setProcessEnvironment(processEnvironment);
    process.start(git, arguments);
    if (!process.waitForStarted())
        throw GitError(QObject::tr("Cannot start git: %1").arg(process.errorString()));
    if (!options.input.isEmpty())
        process.write(options.input);
    process.closeWriteChannel();

    // No timeout: the repository's hooks (pre-commit…) can take a while, and
    // a fetch over a slow network too. Only the user stops git.
    QByteArray errorOutput;
    QByteArray progressBuffer;
    const auto readErrors = [&] {
        const QByteArray chunk = process.readAllStandardError();
        errorOutput += chunk;
        if (options.feedback.progress) {
            progressBuffer += chunk;
            for (const QString &line : takeLines(progressBuffer))
                options.feedback.progress(line);
        }
    };
    while (!process.waitForFinished(pollMs) && process.state() != QProcess::NotRunning) {
        readErrors();
        if (options.feedback.cancel && options.feedback.cancel->load()) {
            process.kill();
            process.waitForFinished();
            throw GitError(QObject::tr("Cancelled"), GitError::Kind::Cancelled);
        }
    }
    readErrors();

    const QByteArray output = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString message =
            options.feedback.progress ? withoutProgress(errorOutput) : QString::fromUtf8(errorOutput).trimmed();
        if (message.isEmpty())
            message = QString::fromUtf8(output).trimmed();
        if (message.isEmpty())
            message = QObject::tr("git %1 failed (code %2)").arg(arguments.value(0)).arg(process.exitCode());
        throw GitError(message);
    }
    return output;
}

QByteArray run(const QString &workDir,
               const QStringList &arguments,
               const QByteArray &input,
               const QHash<QString, QString> &environment)
{
    return run(workDir, arguments, RunOptions{input, environment, {}});
}

QHash<QString, QString> englishMessages()
{
    // LC_ALL wins over the user's LANG and LC_MESSAGES; C.UTF-8 keeps
    // non-ASCII text (paths, branch names) intact.
    return {{QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8")}, {QStringLiteral("LANGUAGE"), QString()}};
}

QStringList splitNul(const QByteArray &output)
{
    QStringList result;
    for (const QByteArray &field : output.split('\0')) {
        if (!field.isEmpty())
            result.append(QString::fromUtf8(field));
    }
    return result;
}

} // namespace Git
