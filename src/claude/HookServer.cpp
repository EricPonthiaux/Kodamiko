// SPDX-License-Identifier: GPL-3.0-only

#include "HookServer.h"

#include "mcp/McpSession.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QPointer>
#include <QRandomGenerator>

namespace {

// The hook's output, kept on the socket until the reply.
constexpr const char *outputProperty = "duetHookOutput";

} // namespace

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
    if (QJsonDocument::fromJson(line).object().value(QLatin1String("duet")).toString() == QLatin1String("mcp")) {
        // The MCP relay: the connection stays open for its session.
        disconnect(socket, &QLocalSocket::readyRead, this, nullptr);
        if (mcpRegistry_)
            new McpSession(socket, mcpRegistry_, this);
        else
            socket->disconnectFromServer();
        return;
    }
    receiving_ = socket;
    replyDeferred_ = false;
    if (const std::optional<HookEvent> event = HookEvent::fromJson(line))
        emit hookReceived(*event);
    receiving_ = nullptr;
    if (!replyDeferred_)
        reply(socket);
}

std::function<void()> HookServer::deferReply()
{
    if (!receiving_)
        return [] { };
    replyDeferred_ = true;
    return [socket = QPointer<QLocalSocket>(receiving_)] {
        if (socket)
            reply(socket);
    };
}

void HookServer::respond(const QJsonObject &output)
{
    if (receiving_)
        receiving_->setProperty(outputProperty, QJsonDocument(output).toJson(QJsonDocument::Compact));
}

void HookServer::reply(QLocalSocket *socket)
{
    const QByteArray output = socket->property(outputProperty).toByteArray();
    socket->write(output.isEmpty() ? QByteArray("ok\n") : "ok " + output + '\n');
    socket->flush();
    socket->disconnectFromServer();
}
