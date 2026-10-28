// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "HookEvent.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QSet>

#include <functional>

class AgentProvider;
class McpRegistry;
class QJsonObject;

// Receives notifications from an agent's hooks, relayed by the small
// `kodamiko-hook` program through a local socket (named pipe on Windows), and
// decoded by the agent's provider.
//
// Protocol: the hook sends one JSON line; the server emits hookReceived()
// — slots connected with a direct connection run immediately — then
// replies "ok". The hook waits for this reply before handing control back
// to the agent. A slot can postpone the reply with deferReply(), to finish
// work off the UI thread before the agent goes on, and answer the hook with
// respond(): the reply is then "ok <json>", which the hook prints for
// the agent to read (additional context, decision…).
//
// The same socket serves the kodamiko-mcp relay, the editor's MCP server for
// the agent: a connection whose first line is {"kodamiko":"mcp"} stays open and is
// handed to an McpSession serving the registry's tools.
class HookServer : public QObject {
    Q_OBJECT

public:
    explicit HookServer(QObject *parent = nullptr);
    ~HookServer() override;
    Q_DISABLE_COPY_MOVE(HookServer)

    bool listen();

    // Full name to pass to the hook (KDK_SOCKET variable).
    QString serverName() const { return server_.fullServerName(); }

    QString errorString() const { return server_.errorString(); }

    // Only from a hookReceived() slot: the reply is sent when the returned
    // function is called (once), not when the slot returns. The hook gives
    // up waiting after a few seconds.
    std::function<void()> deferReply();
    // The same, the reply carrying the JSON passed to the returned function
    // (as respond() would), or none for an empty object.
    std::function<void(const QJsonObject &output)> deferResponse();
    // The same, and `abandoned` runs if the hook gives up waiting before the
    // reply (the agent went on without it), as long as `context` lives.
    std::function<void(const QJsonObject &output)> deferResponse(QObject *context, std::function<void()> abandoned);

    // Only from a hookReceived() slot: the JSON the hook prints on its
    // standard output, sent with the reply (deferred or not).
    void respond(const QJsonObject &output);

    // The agent whose hooks the server decodes; without one, hooks get
    // their reply but emit nothing. Not owned.
    void setProvider(const AgentProvider *provider) { provider_ = provider; }

    // The tools, resources and prompts MCP connections serve; without a
    // registry, they are closed.
    void setMcpRegistry(McpRegistry *registry) { mcpRegistry_ = registry; }

signals:
    void hookReceived(const HookEvent &event);

private:
    void onNewConnection();
    void onReadyRead(QLocalSocket *socket);
    static void reply(QLocalSocket *socket);

    QLocalServer server_;
    QPointer<QLocalSocket> receiving_; // during hookReceived()
    QSet<QLocalSocket *> handling_;    // in onReadyRead(): deleted after, not on disconnection
    bool replyDeferred_ = false;
    const AgentProvider *provider_ = nullptr;
    McpRegistry *mcpRegistry_ = nullptr;
};
