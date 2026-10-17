#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

// Notification sent by a Claude Code hook, or by the status line command
// (see ClaudeLauncher).
struct HookEvent {
    enum class Kind {
        PromptSubmitted,     // UserPromptSubmit: Claude starts working on a prompt
        EditRequested,       // PreToolUse on Edit/Write/MultiEdit/NotebookEdit: Claude is about to edit
        FilesEdited,         // PostToolUse on Edit/Write/MultiEdit/NotebookEdit
        CommandFinished,     // PostToolUse on Bash: any file may have changed
        PermissionRequested, // PermissionRequest: Claude waits for an approval
        NeedsAttention,      // Notification: Claude waits for the user
        ResponseFinished,    // Stop or StopFailure: end of the response
        StatusUpdated,       // status line refresh: model, context, usage limits
    };

    Kind kind;
    QString sessionId;
    QStringList filePaths; // EditRequested, FilesEdited: absolute paths
    QString prompt;        // PromptSubmitted
    // EditRequested, PermissionRequested: the tool name; NeedsAttention: Claude's message;
    // ResponseFinished: the error, if any.
    QString message;
    bool failed = false; // ResponseFinished after an API error (StopFailure)
    QJsonObject status;  // StatusUpdated: the JSON Claude Code passes to status lines

    // Decodes the JSON passed by Claude Code on the hook's input;
    // nullopt for invalid JSON or an event that does not concern us.
    static std::optional<HookEvent> fromJson(const QByteArray &json);
};
