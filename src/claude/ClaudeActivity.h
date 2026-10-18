#pragma once

#include "HookEvent.h"

#include <QObject>

// What the Claude session is doing, deduced from its hooks: shown as a dot
// on its tab, and used to notify the user when Claude needs them or has
// finished while they were looking elsewhere.
class ClaudeActivity : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,       // nothing in progress, nothing new
        Working,    // a response is in progress
        NeedsInput, // Claude waits for an approval or an answer
        Done,       // a response finished while the user was not watching
    };

    explicit ClaudeActivity(QObject *parent = nullptr);

    State state() const { return state_; }

    // Explanation for the tab tooltip ("Waiting for permission: Bash").
    QString description() const;

    void handle(const HookEvent &event);
    // The user interrupted the response (Escape): Claude Code sends no Stop.
    void interrupt();
    // New or finished session.
    void reset();
    // Whether the user is looking at the session (window active, Claude tab
    // displayed). Watching acknowledges a finished response.
    void setWatched(bool watched);

signals:
    void stateChanged(ClaudeActivity::State state);
    // Claude needs the user, or finished, while they were not watching.
    void attentionRequested(const QString &title, const QString &message);

private:
    void setState(State state, const QString &detail = {});
    void waitForInput(const HookEvent &event);
    void finishResponse(const HookEvent &event);

    State state_ = State::Idle;
    QString detail_;
    bool watched_ = false;
};
