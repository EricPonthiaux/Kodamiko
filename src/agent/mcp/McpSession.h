// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "McpRegistry.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>

class QLocalSocket;

// One MCP connection: the kodamiko-mcp relay Claude Code started, connected to
// the editor's local socket. Speaks JSON-RPC 2.0, one message per line (MCP's
// stdio transport, relayed as is): the lifecycle (initialize, ping), tools,
// resources with subscriptions, prompts. The session lives as long as the
// connection.
class McpSession : public QObject {
    Q_OBJECT

public:
    // Takes over `socket`, whose handshake line was already read.
    McpSession(QLocalSocket *socket, McpRegistry *registry, QObject *parent = nullptr);

    // The protocol versions served, newest first.
    static const QStringList &protocolVersions();
    // Handles one JSON-RPC message; answers through `send`. Public for tests.
    void handleMessage(const QJsonObject &message);

signals:
    // A message for the client (tests listen to it).
    void sent(const QJsonObject &message);
    void closed();

private:
    void readLines();
    void send(const QJsonObject &message);
    void result(const QJsonValue &id, const QJsonObject &value);
    void error(const QJsonValue &id, int code, const QString &message);
    // One JSON-RPC method: answers the request `id`.
    using RequestHandler = void (McpSession::*)(const QJsonValue &id, const QJsonObject &params);
    void handleRequest(const QString &method, const QJsonValue &id, const QJsonObject &params);
    void initialize(const QJsonValue &id, const QJsonObject &params);
    // Answers with an empty result (ping, logging level).
    void acknowledge(const QJsonValue &id, const QJsonObject &params);
    void listTools(const QJsonValue &id, const QJsonObject &params);
    void callTool(const QJsonValue &id, const QJsonObject &params);
    void listResources(const QJsonValue &id, const QJsonObject &params);
    void listResourceTemplates(const QJsonValue &id, const QJsonObject &params);
    void readResource(const QJsonValue &id, const QJsonObject &params);
    void subscribe(const QJsonValue &id, const QJsonObject &params);
    void unsubscribe(const QJsonValue &id, const QJsonObject &params);
    void listPrompts(const QJsonValue &id, const QJsonObject &params);
    void getPrompt(const QJsonValue &id, const QJsonObject &params);
    // Reports a long tool's progress to the client when it gave a token.
    McpProgress progressFor(const QJsonValue &token);

    QPointer<QLocalSocket> socket_;
    McpRegistry *registry_;
    QSet<QString> subscriptions_;
    bool initialized_ = false;
};
