// enlumine-hook : relais entre les hooks de Claude Code et l'éditeur.
//
// Claude Code lance ce programme à chaque événement déclaré dans les hooks
// (voir ClaudeLauncher) et lui passe l'événement en JSON sur l'entrée
// standard. On le transmet tel quel à l'éditeur par le socket local nommé
// dans ENLUMINE_SOCKET, puis on attend son accusé de réception.
//
// Ce programme n'écrit jamais sur sa sortie standard (Claude Code pourrait
// l'interpréter) et sort toujours avec le code 0 : un éditeur fermé ou
// injoignable ne doit jamais bloquer Claude.

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>

namespace {

constexpr int connectTimeoutMs = 2000;
// L'éditeur peut recalculer le diff git avant de répondre.
constexpr int acknowledgeTimeoutMs = 10000;

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString serverName = qEnvironmentVariable("ENLUMINE_SOCKET");
    if (serverName.isEmpty())
        return 0;

    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly))
        return 0;
    const QJsonDocument event = QJsonDocument::fromJson(input.readAll());
    if (event.isNull())
        return 0;

    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(connectTimeoutMs))
        return 0;

    socket.write(event.toJson(QJsonDocument::Compact) + '\n');
    socket.waitForBytesWritten(connectTimeoutMs);
    while (!socket.canReadLine() && socket.waitForReadyRead(acknowledgeTimeoutMs)) {
    }
    return 0;
}
