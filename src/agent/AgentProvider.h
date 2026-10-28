// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "HookEvent.h"
#include "terminal/ProcessSpec.h"

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class AgentMark;

enum class AgentProviderId {
    ClaudeCode,
};

// What a provider's sessions can do beyond the hooks every provider has.
struct AgentFeatures {
    // Its sessions message each other: the worktree sessions are told to
    // coordinate that way.
    bool hasAgentMessaging = false;
    // It runs without a session to write a file (fileRunProcess()): the
    // editor offers to have it draw diagrams.
    bool hasFileRuns = false;
};

// A model the agent can run, for the user to pick one (the reviewer's).
struct AgentModel {
    QString id;    // as the agent's command line takes it: "opus"
    QString title; // in menus: "Opus"
};

// How a session conversation begins.
enum class SessionStart {
    New,
    Resume, // resumes the conversation `AgentSessionOptions::conversationId`
};

// What a session is started with, besides its folder and the editor's hooks.
struct AgentSessionOptions {
    SessionStart start = SessionStart::New;
    // With them, the session gets the editor's MCP server (duet-mcp), these
    // of its tools allowed without asking.
    std::optional<QStringList> mcpReadOnlyTools;
    // The name other sessions address it by; without one, the agent picks
    // one.
    QString name;
    // Its identifier (a UUID), which its hooks carry: tells its events from
    // the other sessions'. Without one, the agent picks it.
    QString sessionId;
    // With SessionStart::Resume, the session identifier of the conversation
    // to resume.
    QString conversationId;
    // Added to the agent's system prompt (the instructions of a worktree
    // session, of the project's recipe); empty for none.
    QString instructions;
    // Plugin folders of skills the session loads besides Duet's own (the
    // project's recipe's); an agent without plugins ignores them.
    QStringList pluginFolders;
};

// A run without a session, outside the terminal, that writes one file (a
// diagram): the agent may read the project and write `relativePath` only.
struct AgentFileRun {
    QString workingDirectory;
    QString prompt;
    QString relativePath; // of the file to write, from workingDirectory
    // The HookServer its hooks report each write to, for the editor to check
    // the file; empty for none.
    QString hookServerName;
};

// A run without a session, outside the terminal, that reviews code: the
// agent may read the project and change nothing. It starts from a blank
// conversation, so that it judges the code without the reasoning of the
// session that wrote it.
struct AgentReviewRun {
    QString workingDirectory;
    QString prompt;
    // The JSON Schema the agent's answer follows (ReviewReport::schema()).
    QJsonObject answerSchema;
    // The model that reviews, as the agent names it ("opus"); empty for the
    // agent's default.
    QString model;
};

// What a line of a file run's or a review run's output tells
// (parseRunOutput()).
struct AgentRunEvent {
    enum class Kind {
        None,
        Steps,
        Finished,
    };
    Kind kind = Kind::None;
    QStringList steps;   // Steps: what the agent does, "Reading src/a.cpp"
    bool failed = false; // Finished
    QString text;        // Finished: the agent's answer, or the error
};

// What the user answers to a PermissionRequested hook.
enum class PermissionAnswer {
    Allow,
    // Allow, and remember it with the rules the agent suggested
    // (HookEvent::permissionSuggestions).
    AlwaysAllow,
    Deny,
};

// An agent CLI that Duet runs in a terminal (Claude Code…): its name and
// look in the editor, how to start it, and the language of its hooks. The
// rest of the editor deals with any agent through this interface and the
// HookEvents it decodes.
//
// A provider is stateless and lives as long as the application: it is
// handed around by pointer or reference, never owned (see AgentProviders).
class AgentProvider {
public:
    virtual ~AgentProvider() = default;

    virtual AgentProviderId id() const = 0;
    // The word the editor names the agent with ("Claude"), in menus, tabs
    // and messages.
    virtual QString name() const = 0;
    // Its icon among UiIcons, and the color it is drawn in at rest.
    virtual QString iconName() const = 0;
    virtual QColor brandColor() const = 0;
    // Its mark in the status bar's indicator.
    virtual const AgentMark &mark() const = 0;
    virtual AgentFeatures features() const = 0;

    // The program found, or an empty string when the agent is not installed.
    virtual QString findExecutable() const = 0;
    // The interactive session to run in `workingDirectory`, its hooks
    // reporting to the HookServer named `hookServerName`; nullopt (with
    // `error` set) when the agent or duet-hook cannot be found.
    virtual std::optional<ProcessSpec> sessionProcess(const QString &workingDirectory,
                                                      const QString &hookServerName,
                                                      const AgentSessionOptions &options,
                                                      QString *error) const = 0;

    // The process of `run`; nullopt (with `error` set) when the agent cannot
    // be found. Only for a provider with features().hasFileRuns.
    virtual std::optional<ProcessSpec> fileRunProcess(const AgentFileRun &run, QString *error) const = 0;
    // The models a review run may use (AgentReviewRun::model), the most
    // capable first.
    virtual QList<AgentModel> models() const = 0;
    // The process of `run`; nullopt (with `error` set) when the agent cannot
    // be found.
    virtual std::optional<ProcessSpec> reviewRunProcess(const AgentReviewRun &run, QString *error) const = 0;
    // Decodes a line of a file run's or a review run's standard output; file
    // paths in steps are shown relative to `workingDirectory`.
    virtual AgentRunEvent parseRunOutput(const QByteArray &line, const QString &workingDirectory) const = 0;

    // Decodes the JSON a hook passes (relayed by duet-hook); nullopt for
    // invalid JSON or an event the editor does not follow.
    virtual std::optional<HookEvent> parseHook(const QByteArray &json) const = 0;
    // The reply to a PromptSubmitted hook that adds `context` to the prompt.
    virtual QJsonObject promptContextReply(const QString &context) const = 0;
    // The reply to an EditRequested or CommandRequested hook that refuses the
    // edit or the command for `reason`.
    virtual QJsonObject denyToolReply(const QString &reason) const = 0;
    // The reply to a FilesEdited hook that sends `reason` back to the agent
    // to fix what it wrote.
    virtual QJsonObject blockReply(const QString &reason) const = 0;
    // The reply to a FilesEdited hook that tells the agent `context` along
    // with the edit's result, without refusing anything.
    virtual QJsonObject editContextReply(const QString &context) const = 0;
    // The reply to the PermissionRequested hook `request` that gives the
    // user's `answer`.
    virtual QJsonObject permissionReply(const HookEvent &request, PermissionAnswer answer) const = 0;
};
