// SPDX-License-Identifier: GPL-3.0-only

#include "HookServer.h"

#include "AgentProvider.h"
#include "agent/mcp/McpSession.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QPointer>
#include <QRandomGenerator>

#include <utility>

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
    // A slot may open a modal popup: its event loop can deliver another hook
    // (hence the outer state, put back after), and the hook can give up
    // waiting meanwhile, which deletes the socket.
    const QPointer<QLocalSocket> outerReceiving = std::exchange(receiving_, socket);
    const bool outerReplyDeferred = std::exchange(replyDeferred_, false);
    if (const std::optional<HookEvent> event = provider_ ? provider_->parseHook(line) : std::nullopt)
        emit hookReceived(*event);
    const bool isReplyDeferred = std::exchange(replyDeferred_, outerReplyDeferred);
    const QPointer<QLocalSocket> stillOpen = std::exchange(receiving_, outerReceiving);
    if (!isReplyDeferred && stillOpen)
        reply(stillOpen);
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

// SIGTERM
void HookServer::reply(QLocalSocket *socket)
{
    const QByteArray output = socket->property(outputProperty).toByteArray();
    socket->write(output.isEmpty() ? QByteArray("ok\n") : "ok " + output + '\n');
    socket->flush();
    socket->disconnectFromServer();
}
