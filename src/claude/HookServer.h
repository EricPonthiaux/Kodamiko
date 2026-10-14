#pragma once

#include "HookEvent.h"

#include <QLocalServer>
#include <QObject>

class QLocalSocket;

// Reçoit les notifications des hooks Claude Code, relayées par le petit
// programme `enlumine-hook` via un socket local (named pipe sous Windows).
//
// Protocole : le hook envoie une ligne JSON ; le serveur émet hookReceived()
// — les slots branchés en connexion directe s'exécutent aussitôt — puis
// répond « ok ». Le hook attend cette réponse avant de rendre la main à
// Claude.
class HookServer : public QObject {
    Q_OBJECT

public:
    explicit HookServer(QObject *parent = nullptr);

    bool listen();
    // Nom complet à transmettre au hook (variable ENLUMINE_SOCKET).
    QString serverName() const { return server_.fullServerName(); }
    QString errorString() const { return server_.errorString(); }

signals:
    void hookReceived(const HookEvent &event);

private:
    void onNewConnection();
    void onReadyRead(QLocalSocket *socket);

    QLocalServer server_;
};
