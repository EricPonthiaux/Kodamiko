#include "CodeFormatter.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace CodeFormatter {
namespace {

// Longer than any formatter takes on a source file; a formatter waiting for
// something else must not freeze the editor.
constexpr int timeoutMs = 10000;

Command clangFormat(const QString &filePath)
{
    return {QStringLiteral("clang-format"), {QStringLiteral("--assume-filename=") + filePath}};
}

Command prettier(const QString &filePath)
{
    return {QStringLiteral("prettier"), {QStringLiteral("--stdin-filepath"), filePath}};
}

bool isBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Moves `position` back to the start of the UTF-8 character it is in.
int characterStart(const QByteArray &text, int position)
{
    while (position > 0 && position < text.size() && (static_cast<unsigned char>(text[position]) & 0xC0) == 0x80)
        --position;
    return position;
}

// prettier is usually a project dependency rather than a global install.
QString localPrettier(const QString &filePath)
{
    QDir dir = QFileInfo(filePath).absoluteDir();
    do {
        for (const QString &name : {QStringLiteral("prettier"), QStringLiteral("prettier.cmd")}) {
            const QFileInfo program(dir.filePath(QStringLiteral("node_modules/.bin/") + name));
            if (program.isFile() && program.isExecutable())
                return program.absoluteFilePath();
        }
    } while (dir.cdUp());
    return {};
}

} // namespace

QList<Command> candidates(const QString &languageId, const QString &filePath)
{
    static const QStringList clangLanguages = {QStringLiteral("c"), QStringLiteral("cpp"), QStringLiteral("java")};
    // Languages clang-format also reads, when prettier is not there.
    static const QStringList webLanguages = {QStringLiteral("javascript"), QStringLiteral("typescript"),
                                             QStringLiteral("tsx"), QStringLiteral("json")};
    static const QStringList prettierOnlyLanguages = {QStringLiteral("css"), QStringLiteral("html"),
                                                      QStringLiteral("markdown"), QStringLiteral("yaml")};

    if (clangLanguages.contains(languageId))
        return {clangFormat(filePath)};
    if (webLanguages.contains(languageId))
        return {prettier(filePath), clangFormat(filePath)};
    if (prettierOnlyLanguages.contains(languageId))
        return {prettier(filePath)};
    if (languageId == QLatin1String("python")) {
        return {{QStringLiteral("ruff"),
                 {QStringLiteral("format"), QStringLiteral("--stdin-filename"), filePath, QStringLiteral("-")}},
                {QStringLiteral("black"),
                 {QStringLiteral("--quiet"), QStringLiteral("--stdin-filename"), filePath, QStringLiteral("-")}}};
    }
    if (languageId == QLatin1String("bash"))
        return {{QStringLiteral("shfmt"), {QStringLiteral("--filename"), filePath}}};
    if (languageId == QLatin1String("cmake"))
        return {{QStringLiteral("cmake-format"), {QStringLiteral("-")}}};
    return {};
}

std::optional<Command> find(const QString &languageId, const QString &filePath)
{
    for (Command command : candidates(languageId, filePath)) {
        QString program;
        if (command.program == QLatin1String("prettier"))
            program = localPrettier(filePath);
        if (program.isEmpty())
            program = QStandardPaths::findExecutable(command.program);
        if (!program.isEmpty()) {
            command.program = std::move(program);
            return command;
        }
    }
    return std::nullopt;
}

QString describeCandidates(const QString &languageId, const QString &filePath)
{
    QStringList names;
    for (const Command &command : candidates(languageId, filePath))
        names.append(command.program);
    if (names.size() < 2)
        return names.value(0);
    const QString last = names.takeLast();
    return QObject::tr("%1 or %2").arg(names.join(QStringLiteral(", ")), last);
}

namespace {

// The formatted text of a process that ran `command` to completion.
std::optional<QString> resultOf(QProcess &process, const QString &name, const QString &text, QString *error)
{
    const QByteArray output = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        // The first line names the problem (often a syntax error and its position).
        const QString message =
            QString::fromUtf8(process.readAllStandardError()).trimmed().section(QLatin1Char('\n'), 0, 0);
        *error = message.isEmpty() ? QObject::tr("%1 failed (code %2)").arg(name).arg(process.exitCode())
                                   : QStringLiteral("%1: %2").arg(name, message);
        return std::nullopt;
    }
    // An empty output for a non-empty text is a formatter that wrote
    // elsewhere, not a file to empty.
    if (output.isEmpty() && !text.trimmed().isEmpty()) {
        *error = QObject::tr("%1 returned no text").arg(name);
        return std::nullopt;
    }
    return QString::fromUtf8(output);
}

QString timeoutMessage(const QString &name)
{
    return QObject::tr("%1 did not finish within %2 seconds").arg(name).arg(timeoutMs / 1000);
}

} // namespace

std::optional<QString> run(const Command &command, const QString &workDir, const QString &text, QString *error)
{
    const QString name = QFileInfo(command.program).completeBaseName();
    QProcess process;
    process.setWorkingDirectory(workDir);
    process.start(command.program, command.arguments);
    if (!process.waitForStarted()) {
        *error = QObject::tr("Cannot start %1: %2").arg(name, process.errorString());
        return std::nullopt;
    }
    process.write(text.toUtf8());
    process.closeWriteChannel();
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        *error = timeoutMessage(name);
        return std::nullopt;
    }
    return resultOf(process, name, text, error);
}

void start(const Command &command, const QString &workDir, const QString &text, QObject *context, const Done &done)
{
    const QString name = QFileInfo(command.program).completeBaseName();
    auto *process = new QProcess(context);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);

    // Exactly one outcome: the first one disconnects the others.
    const auto finish = [process, timer, done](const std::optional<QString> &formatted, const QString &error) {
        timer->stop();
        QObject::disconnect(process, nullptr, nullptr, nullptr);
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            // Reaped by QProcess once it exits; deleting it now would block.
            QObject::connect(process, &QProcess::finished, process, &QObject::deleteLater);
        } else {
            process->deleteLater();
        }
        done(formatted, error);
    };
    QObject::connect(process, &QProcess::errorOccurred, context, [process, name, finish](QProcess::ProcessError code) {
        if (code == QProcess::FailedToStart)
            finish(std::nullopt, QObject::tr("Cannot start %1: %2").arg(name, process->errorString()));
    });
    QObject::connect(process, &QProcess::finished, context, [process, name, text, finish] {
        QString error;
        const std::optional<QString> formatted = resultOf(*process, name, text, &error);
        finish(formatted, error);
    });
    QObject::connect(timer, &QTimer::timeout, context, [name, finish] { finish(std::nullopt, timeoutMessage(name)); });

    process->setWorkingDirectory(workDir);
    process->start(command.program, command.arguments);
    process->write(text.toUtf8());
    process->closeWriteChannel();
    timer->start(timeoutMs);
}

int mapPosition(const QByteArray &before, int position, const QByteArray &after)
{
    position = std::clamp(position, 0, static_cast<int>(before.size()));
    int nonBlank = 0;
    for (int i = 0; i < position; ++i) {
        if (!isBlank(before[i]))
            ++nonBlank;
    }
    // Just before code, the cursor stays before that code (start of an
    // indented line); elsewhere, it stays after the code that precedes it
    // (end of a line).
    const bool beforeCode = position < before.size() && !isBlank(before[position]);

    int seen = 0;
    for (int i = 0; i < after.size(); ++i) {
        if (isBlank(after[i]))
            continue;
        if (beforeCode && seen == nonBlank)
            return characterStart(after, i);
        ++seen;
        if (!beforeCode && seen == nonBlank)
            return characterStart(after, i + 1);
    }
    return nonBlank == 0 && !beforeCode ? 0 : static_cast<int>(after.size());
}

} // namespace CodeFormatter
