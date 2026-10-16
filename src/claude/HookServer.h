#pragma once

#include "HookEvent.h"

#include <QLocalServer>
#include <QObject>

class QLocalSocket;

// Receives notifications from Claude Code hooks, relayed by the small
// `enlumine-hook` program through a local socket (named pipe on Windows).
//
// Protocol: the hook sends one JSON line; the server emits hookReceived()
// — slots connected with a direct connection run immediately — then
// replies "ok". The hook waits for this reply before handing control back
// to Claude.
class HookServer : public QObject {
    Q_OBJECT

public:
    explicit HookServer(QObject *parent = nullptr);

    bool listen();
    // Full name to pass to the hook (ENLUMINE_SOCKET variable).
    QString serverName() const { return server_.fullServerName(); }
    QString errorString() const { return server_.errorString(); }

signals:
    void hookReceived(const HookEvent &event);

private:
    void onNewConnection();
    void onReadyRead(QLocalSocket *socket);

    QLocalServer server_;
};
