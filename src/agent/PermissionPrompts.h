// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "AgentProvider.h"
#include "HookEvent.h"
#include "ui/Snackbar.h"

#include <QJsonObject>
#include <QObject>

#include <functional>

class HookServer;

// The agents' permission requests, asked in the window's snackbar: Allow,
// Always Allow (when the agent suggests rules to remember it with) or Deny,
// answered to the agent's hook. Closing the snackbar, or a session that
// moves on (a new prompt, the end of its response), leaves the question to
// the agent's terminal; a hook that gives up waiting takes it away.
class PermissionPrompts : public QObject {
    Q_OBJECT

public:
    // Asks in `snackbar` and answers through `hookServer`; neither is owned.
    PermissionPrompts(Snackbar *snackbar, HookServer *hookServer, QObject *parent);

    // The agent whose replies it writes. Not owned.
    void setProvider(const AgentProvider *provider) { provider_ = provider; }

    // For each hook of a session, from a HookServer::hookReceived() slot
    // (the request's reply is deferred there); `asker` names who asks
    // ("Claude", "Claude in agent-1").
    void handle(const HookEvent &event, const QString &asker);

    // The requests waiting for an answer.
    qsizetype pendingCount() const { return pending_.size(); }

private:
    struct Pending {
        quint64 key = 0;
        QString sessionId;
        Snackbar::Id message = 0;
        std::function<void(const QJsonObject &output)> respond;
    };

    void ask(const HookEvent &event, const QString &asker);
    SnackbarMessage messageFor(const HookEvent &event, const QString &asker, quint64 key);
    // Answers request `key` with `output` (empty: the terminal asks).
    void reply(quint64 key, const QJsonObject &output);
    // Takes request `key` away without answering: its hook is gone.
    void forget(quint64 key);
    // Leaves the requests of `sessionId` to its terminal.
    void leaveToTerminal(const QString &sessionId);

    Snackbar *snackbar_;
    HookServer *hookServer_;
    const AgentProvider *provider_ = nullptr;
    QList<Pending> pending_;
    quint64 lastKey_ = 0;
};
