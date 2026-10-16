#include "HookServer.h"

#include <QCoreApplication>
#include <QLocalSocket>
#include <QRandomGenerator>

HookServer::HookServer(QObject *parent)
    : QObject(parent)
{
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, &HookServer::onNewConnection);
}

bool HookServer::listen()
{
    const QString name = QStringLiteral("duet-%1-%2")
                             .arg(QCoreApplication::applicationPid())
                             .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    return server_.listen(name);
}

void HookServer::onNewConnection()
{
    while (QLocalSocket *socket = server_.nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        if (socket->canReadLine())
            onReadyRead(socket);
    }
}

void HookServer::onReadyRead(QLocalSocket *socket)
{
    if (!socket->canReadLine())
        return;
    const QByteArray line = socket->readLine();
    if (const std::optional<HookEvent> event = HookEvent::fromJson(line))
        emit hookReceived(*event);
    socket->write("ok\n");
    socket->flush();
    socket->disconnectFromServer();
}
