// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectWindowServer.h"

#include "WindowForeground.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalSocket>

namespace {

// Long enough for a process busy for a moment; a dead one refuses at once.
constexpr int connectTimeoutMs = 1000;

// The same for every spelling of the folder (symbolic links, trailing
// separator) and distinct per user, the sockets being in a shared directory.
QString serverName(const QString &folder)
{
    const QString canonical = QDir(folder).canonicalPath();
    const QString key = QDir::homePath() + u'\n' + (canonical.isEmpty() ? QDir(folder).absolutePath() : canonical);
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("kodamiko-project-") + QString::fromLatin1(hash);
}

} // namespace

ProjectWindowServer::ProjectWindowServer(QObject *parent)
    : QObject(parent)
{
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, &ProjectWindowServer::onNewConnection);
}

bool ProjectWindowServer::listen(const QString &folder)
{
    server_.close();
    // Takes the socket over from a process that crashed with it.
    QLocalServer::removeServer(serverName(folder));
    return server_.listen(serverName(folder));
}

void ProjectWindowServer::onNewConnection()
{
    while (QLocalSocket *socket = server_.nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        socket->disconnectFromServer();
    }
    emit raiseRequested();
}

bool ProjectWindows::raise(const QString &folder)
{
    WindowForeground::allowOtherProcess();
    QLocalSocket socket;
    socket.connectToServer(serverName(folder));
    if (!socket.waitForConnected(connectTimeoutMs))
        return false;
    socket.disconnectFromServer();
    return true;
}
