// duet-hook: relay between Claude Code and the editor.
//
// Claude Code runs this program for every event declared in the hooks
// (see ClaudeLauncher) and passes it the event as JSON on standard
// input. We forward it as is to the editor over the local socket named
// in DUET_SOCKET, then wait for its acknowledgement.
//
// As a hook, this program never writes to its standard output (Claude Code
// might interpret it) and always exits with code 0: a closed or unreachable
// editor must never block Claude.
//
// With the "statusline" argument, it is Claude Code's status line command:
// it forwards the session state (model, context, usage limits) to the
// editor, then prints the line to display: the output of the user's own
// status line command (DUET_STATUSLINE_COMMAND) if there is one, a
// short summary otherwise.

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>

#include <cstdio>

namespace {

constexpr int connectTimeoutMs = 2000;
// The editor may take a snapshot or recompute the git diff before replying.
constexpr int acknowledgeTimeoutMs = 10000;
// Claude Code cancels a status line that is still running when the next
// update arrives: no need to wait long.
constexpr int statusLineTimeoutMs = 5000;

void sendToEditor(const QJsonDocument &event, int acknowledgeTimeout)
{
    const QString serverName = qEnvironmentVariable("DUET_SOCKET");
    if (serverName.isEmpty())
        return;
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(connectTimeoutMs))
        return;
    socket.write(event.toJson(QJsonDocument::Compact) + '\n');
    socket.waitForBytesWritten(connectTimeoutMs);
    while (!socket.canReadLine() && socket.waitForReadyRead(acknowledgeTimeout)) {
    }
}

QByteArray runUserStatusLine(const QString &command, const QByteArray &input)
{
    QProcess process;
#ifdef Q_OS_WIN
    process.setProgram(QStringLiteral("cmd.exe"));
    process.setNativeArguments(QStringLiteral("/c ") + command);
#else
    QString shell = QStandardPaths::findExecutable(QStringLiteral("bash"));
    if (shell.isEmpty())
        shell = QStringLiteral("/bin/sh");
    process.setProgram(shell);
    process.setArguments({QStringLiteral("-c"), command});
#endif
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process.start();
    if (!process.waitForStarted())
        return {};
    process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(statusLineTimeoutMs)) {
        process.kill();
        process.waitForFinished();
    }
    return process.readAllStandardOutput();
}

// "Opus · context 42%"
QByteArray defaultStatusLine(const QJsonObject &status)
{
    QString line = status.value(QLatin1String("model")).toObject().value(QLatin1String("display_name")).toString();
    const QJsonValue context = status.value(QLatin1String("context_window")).toObject().value(QLatin1String("used_percentage"));
    if (context.isDouble()) {
        if (!line.isEmpty())
            line += QStringLiteral(" · ");
        line += QStringLiteral("context %1%").arg(qRound(context.toDouble()));
    }
    return line.toUtf8() + '\n';
}

int statusLine(const QByteArray &input)
{
    const QJsonObject status = QJsonDocument::fromJson(input).object();
    sendToEditor(QJsonDocument(QJsonObject{{QLatin1String("hook_event_name"), QLatin1String("DuetStatusLine")},
                                           {QLatin1String("status"), status}}),
                 connectTimeoutMs);

    const QString userCommand = qEnvironmentVariable("DUET_STATUSLINE_COMMAND");
    const QByteArray line = userCommand.isEmpty() ? defaultStatusLine(status) : runUserStatusLine(userCommand, input);
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly))
        return 0;
    const QByteArray json = input.readAll();

    if (app.arguments().value(1) == QLatin1String("statusline"))
        return statusLine(json);

    const QJsonDocument event = QJsonDocument::fromJson(json);
    if (!event.isNull())
        sendToEditor(event, acknowledgeTimeoutMs);
    return 0;
}
