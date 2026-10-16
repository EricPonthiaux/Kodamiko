#include "GitProcess.h"

#include "FileChange.h"

#include <QProcess>
#include <QStandardPaths>

namespace Git {

QByteArray run(const QString &workDir, const QStringList &arguments, const QByteArray &input,
               const QHash<QString, QString> &environment)
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty())
        throw GitError(QObject::tr("The git command was not found: install git and make sure it is in the PATH."));

    QProcessEnvironment processEnvironment = QProcessEnvironment::systemEnvironment();
    processEnvironment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    processEnvironment.insert(QStringLiteral("GIT_LITERAL_PATHSPECS"), QStringLiteral("1"));
    // git recognizes ":" as an editor that does nothing.
    processEnvironment.insert(QStringLiteral("GIT_EDITOR"), QStringLiteral(":"));
    for (auto it = environment.begin(); it != environment.end(); ++it)
        processEnvironment.insert(it.key(), it.value());

    QProcess process;
    process.setWorkingDirectory(workDir);
    process.setProcessEnvironment(processEnvironment);
    process.start(git, arguments);
    if (!process.waitForStarted())
        throw GitError(QObject::tr("Cannot start git: %1").arg(process.errorString()));
    if (!input.isEmpty())
        process.write(input);
    process.closeWriteChannel();
    // No timeout: the repository's hooks (pre-commit…) can take a while.
    process.waitForFinished(-1);

    const QByteArray output = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString message = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (message.isEmpty())
            message = QString::fromUtf8(output).trimmed();
        if (message.isEmpty())
            message = QObject::tr("git %1 failed (code %2)").arg(arguments.value(0)).arg(process.exitCode());
        throw GitError(message);
    }
    return output;
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
