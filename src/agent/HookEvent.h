// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

// What an agent's hook, or its status line command, tells the editor: the
// same events whatever the agent, decoded by its AgentProvider. The names
// in parentheses are Claude Code's.
struct HookEvent {
    enum class Kind {
        PromptSubmitted,     // the agent starts working on a prompt (UserPromptSubmit)
        EditRequested,       // the agent is about to edit files (PreToolUse on Edit, Write…)
        CommandRequested,    // the agent is about to run a shell command (PreToolUse on Bash)
        FilesEdited,         // the agent edited files (PostToolUse on Edit, Write…)
        CommandFinished,     // a command ran: any file may have changed (PostToolUse on Bash)
        PermissionRequested, // the agent waits for an approval (PermissionRequest)
        NeedsAttention,      // the agent waits for the user (Notification)
        ResponseFinished,    // end of the response (Stop, StopFailure)
        StatusUpdated,       // status line refresh: model, context, usage limits
    };

    Kind kind{};
    QString sessionId;
    // The session's current directory: it outlives the session id, which
    // the agent may replace (/clear, /resume).
    QString workingDirectory;
    QStringList filePaths; // EditRequested, FilesEdited: absolute paths
    QString prompt;        // PromptSubmitted
    // EditRequested, PermissionRequested: the tool name; CommandRequested:
    // the command line; NeedsAttention: the agent's message;
    // ResponseFinished: the error, if any.
    QString message;
    bool failed = false; // ResponseFinished after an API error
    QJsonObject status;  // StatusUpdated: the JSON the agent passes to status lines
};
