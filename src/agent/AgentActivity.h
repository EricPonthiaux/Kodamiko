// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "HookEvent.h"

#include <QObject>

// What an agent session is doing, deduced from its hooks: shown as a dot
// on its tab, and used to notify the user when the agent needs them or has
// finished while they were looking elsewhere.
class AgentActivity : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,       // nothing in progress, nothing new
        Working,    // a response is in progress
        NeedsInput, // the agent waits for an approval or an answer
        Done,       // a response finished while the user was not watching
    };

    explicit AgentActivity(QObject *parent = nullptr);

    // The agent's name in the messages ("Claude is working").
    void setAgentName(const QString &name) { agentName_ = name; }

    State state() const { return state_; }

    // Explanation for the tab tooltip ("Waiting for permission: Bash").
    QString description() const;

    void handle(const HookEvent &event);
    // The user interrupted the response (Escape): the agent sends no hook.
    void interrupt();
    // New or finished session.
    void reset();
    // Whether the user is looking at the session (window active, its tab
    // displayed). Watching acknowledges a finished response.
    void setWatched(bool watched);

signals:
    void stateChanged(AgentActivity::State state);
    // The agent needs the user, or finished, while they were not watching.
    void attentionRequested(const QString &title, const QString &message);

private:
    void setState(State state, const QString &detail = {});
    void waitForInput(const HookEvent &event);
    void finishResponse(const HookEvent &event);

    QString agentName_;
    State state_ = State::Idle;
    QString detail_;
    bool watched_ = false;
};
