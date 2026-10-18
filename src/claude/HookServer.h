#pragma once

#include "HookEvent.h"

#include <QLocalServer>
#include <QObject>

#include <functional>

class McpRegistry;
class QJsonObject;
class QLocalSocket;

// Receives notifications from Claude Code hooks, relayed by the small
// `duet-hook` program through a local socket (named pipe on Windows).
//
// Protocol: the hook sends one JSON line; the server emits hookReceived()
// — slots connected with a direct connection run immediately — then
// replies "ok". The hook waits for this reply before handing control back
// to Claude. A slot can postpone the reply with deferReply(), to finish
// work off the UI thread before Claude goes on, and answer the hook with
// respond(): the reply is then "ok <json>", which the hook prints for
// Claude Code to read (additional context, decision…).
//
// The same socket serves the duet-mcp relay, the editor's MCP server for
// Claude: a connection whose first line is {"duet":"mcp"} stays open and is
// handed to an McpSession serving the registry's tools.
class HookServer : public QObject {
    Q_OBJECT

public:
    explicit HookServer(QObject *parent = nullptr);

    bool listen();

    // Full name to pass to the hook (DUET_SOCKET variable).
    QString serverName() const { return server_.fullServerName(); }

    QString errorString() const { return server_.errorString(); }

    // Only from a hookReceived() slot: the reply is sent when the returned
    // function is called (once), not when the slot returns. The hook gives
    // up waiting after a few seconds.
    std::function<void()> deferReply();

    // Only from a hookReceived() slot: the JSON the hook prints on its
    // standard output, sent with the reply (deferred or not).
    void respond(const QJsonObject &output);

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
    QLocalSocket *receiving_ = nullptr; // during hookReceived()
    bool replyDeferred_ = false;
    McpRegistry *mcpRegistry_ = nullptr;
};
