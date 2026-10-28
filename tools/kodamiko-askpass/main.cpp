// SPDX-License-Identifier: GPL-3.0-only

// kodamiko-askpass: asks the editor's user for what git or ssh needs.
//
// git (GIT_ASKPASS) and ssh (SSH_ASKPASS) run this program with a prompt
// as argument ("Password for 'https://user@example.com':") and read the
// answer on its standard output. We relay the prompt to the editor over
// the local socket named in KDK_ASKPASS_SOCKET (see AskpassServer) and
// wait for the user, as long as it takes.
//
// Exit code 1 when the user cancels or the editor cannot be reached: git
// then fails with an authentication error instead of waiting.

#include "log/LogFiles.h"
#include "system/StandardStreams.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

#include <cstdio>

namespace {

constexpr int connectTimeoutMs = 2000;

} // namespace

int main(int argc, char *argv[])
{
    const QCoreApplication app(argc, argv);
    LogFiles::install(QStringLiteral("kodamiko-askpass"));

    const QString serverName = qEnvironmentVariable("KDK_ASKPASS_SOCKET");
    if (serverName.isEmpty())
        return 1;
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(connectTimeoutMs))
        return 1;

    const QString prompt = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    socket.write(QJsonDocument(QJsonObject{{QLatin1String("prompt"), prompt}}).toJson(QJsonDocument::Compact) + '\n');
    socket.waitForBytesWritten(connectTimeoutMs);
    while (!socket.canReadLine() && socket.waitForReadyRead(-1)) { }
    if (!socket.canReadLine())
        return 1;

    const QJsonObject reply = QJsonDocument::fromJson(socket.readLine()).object();
    if (!reply.contains(QLatin1String("answer")))
        return 1;
    const QByteArray answer = reply.value(QLatin1String("answer")).toString().toUtf8() + '\n';
    // The answer as it is, without a "\r\n" on Windows.
    StandardStreams::useBinaryMode();
    std::fwrite(answer.constData(), 1, static_cast<size_t>(answer.size()), stdout);
    return 0;
}
