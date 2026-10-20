// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ClaudeActivity.h"
#include "HookEvent.h"
#include "git/Worktrees.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <cstddef>
#include <memory>
#include <optional>
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
        // Where its hooks come from (the worktree, symbolic links resolved):
        // they still name it once /clear or /resume replaced its session id.
        QString directory;
        // The last commit the merge queue sent back: its work is not queued
        // again before it commits anew.
        QString sentBackHead;
        // Waits to be typed in its prompt until Claude asks for one.
        QString pendingReport;
    };

    explicit AgentSessions(QObject *parent = nullptr);

    // Follows the session started with `sessionId` in `worktree`, running in
    // `terminal`. It is forgotten when the terminal is destroyed.
    void add(const Worktree &worktree, const QString &sessionId, TerminalWidget *terminal);

    // The session running in `terminal`, or null if it is not an agent's.
    const Agent *find(const QWidget *terminal) const;

    // The session the hook event comes from, or null if it is not an agent's:
    // the one with its session id, else the one whose worktree holds its
    // directory.
    const Agent *find(const HookEvent &event) const;

    // The agent named `name` ("agent-1"), or null.
    const Agent *findByName(const QString &name) const;

    // Whether the hook event comes from one of these sessions.
    bool owns(const HookEvent &event) const;

    bool isEmpty() const { return agents_.empty(); }

    // The sessions' terminals, those still open.
    QList<TerminalWidget *> terminals() const;

    // The agent's commits up to `commit` are merged: work to merge starts
    // after it.
    void setBaseCommit(const QString &agentName, const QString &commit);

    // Updates the activity of the session the event comes from, and its
    // session id when it changed.
    void handle(const HookEvent &event);

    // The merge queue sent back the agent's commits up to `head` with
    // `report`: typed in its prompt once Claude waits for one, never while it
    // waits for an approval, which the Enter would give.
    void sendBack(const QString &agentName, const QString &report, const QString &head);

    // The terminal the user is looking at, null if none: watching an agent's
    // tab acknowledges its finished response.
    void setWatchedTerminal(const QWidget *terminal);

signals:
    void activityChanged(QWidget *terminal, ClaudeActivity::State state);
    // An agent needs the user, or finished, while they were elsewhere.
    void attentionRequested(const QString &title, const QString &message);
    // A session is no longer followed: its terminal was destroyed.
    void sessionForgotten();

private:
    std::optional<std::size_t> indexOf(const HookEvent &event) const;
    void deliverPendingReport(const QWidget *terminal);
    void forget(const QObject *terminal);

    std::vector<Agent> agents_;
};
