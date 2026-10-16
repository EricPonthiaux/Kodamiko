#pragma once

#include "HookEvent.h"

#include <QLocalServer>
#include <QObject>

#include <functional>

class QLocalSocket;

// Receives notifications from Claude Code hooks, relayed by the small
// `duet-hook` program through a local socket (named pipe on Windows).
//
// Protocol: the hook sends one JSON line; the server emits hookReceived()
// — slots connected with a direct connection run immediately — then
// replies "ok". The hook waits for this reply before handing control back
// to Claude. A slot can postpone the reply with deferReply(), to finish
// work off the UI thread before Claude goes on.
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

signals:
    void hookReceived(const HookEvent &event);

private:
    void onNewConnection();
    void onReadyRead(QLocalSocket *socket);
    static void reply(QLocalSocket *socket);

    QLocalServer server_;
    QLocalSocket *receiving_ = nullptr; // during hookReceived()
    bool replyDeferred_ = false;
};
