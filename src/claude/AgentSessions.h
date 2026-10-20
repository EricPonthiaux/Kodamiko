// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ClaudeActivity.h"
#include "HookEvent.h"
#include "git/Worktrees.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <vector>

class TerminalWidget;

// The Claude sessions Duet runs besides the main one, each in a worktree of
// its own (see Worktrees), and what each is doing, deduced from its hooks.
// Their hooks never reach the editor: it shows the main checkout, where
// these sessions edit nothing.
class AgentSessions : public QObject {
    Q_OBJECT

public:
    struct Agent {
        Worktree worktree;
        QString sessionId;
        QPointer<TerminalWidget> terminal; // its tab, owned by the terminal tabs
        std::unique_ptr<ClaudeActivity> activity;
        QString lastPrompt; // what the user last asked it, empty before
    };

    explicit AgentSessions(QObject *parent = nullptr);

    // Follows the session started with `sessionId` in `worktree`, running in
    // `terminal`. It is forgotten when the terminal is destroyed.
    void add(const Worktree &worktree, const QString &sessionId, TerminalWidget *terminal);

    // The session running in `terminal`, or null if it is not an agent's.
    const Agent *find(const QWidget *terminal) const;

    // The session the hook event comes from, or null if it is not an agent's.
    const Agent *find(const HookEvent &event) const;

    // The agent named `name` ("agent-1"), or null.
    const Agent *findByName(const QString &name) const;

    // Whether the hook event comes from one of these sessions.
    bool owns(const HookEvent &event) const;

    // The agent's commits up to `commit` are merged: work to merge starts
    // after it.
    void setBaseCommit(const QString &agentName, const QString &commit);

    // Updates the activity of the session the event comes from.
    void handle(const HookEvent &event);

    // The terminal the user is looking at, null if none: watching an agent's
    // tab acknowledges its finished response.
    void setWatchedTerminal(const QWidget *terminal);

signals:
    void activityChanged(QWidget *terminal, ClaudeActivity::State state);
    // An agent needs the user, or finished, while they were elsewhere.
    void attentionRequested(const QString &title, const QString &message);

private:
    Agent *findBySessionId(const QString &sessionId);
    void forget(const QObject *terminal);

    std::vector<Agent> agents_;
};
