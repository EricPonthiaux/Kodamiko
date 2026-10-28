// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "terminal/ProcessSpec.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

// Prepares launching the `claude` CLI in an editor terminal.
//
// The hooks that report Claude's activity are passed via `--settings`: the
// user's Claude configuration is never modified, and these hooks only
// exist for sessions launched from the editor.
namespace ClaudeLauncher {

// JSON content of the --settings file: hooks that run `hookCommand` on
// the events the editor follows (prompt, file edits, permission requests,
// notifications, end of the response), and a status line that runs
// `hookCommand statusline`. `allowedTools` are allowed without asking (the
// editor's read-only MCP tools). Messages from the other local Claude
// sessions are accepted, so that the sessions of a project coordinate.
// With GitInShell::Checked, the editor also sees each shell command before
// it runs, to send git commands to Duet's tools (GitCommandGuard).
enum class GitInShell {
    Allowed,
    Checked,
};
QByteArray sessionSettings(const QString &hookCommand,
                           const QStringList &allowedTools = {},
                           GitInShell gitInShell = GitInShell::Allowed);
// Where the --settings file of the sessions with Duet's tools is written.
QString sessionSettingsPath();

// Status line command configured by the user for `projectDir` (user,
// project and local settings), or an empty string.
QString userStatusLineCommand(const QString &projectDir);

// The `claude` program found, or an empty string.
QString findClaudeExecutable();

// The command that runs duet-hook, quoted for sh and cmd.exe, or an empty
// string if the program is missing.
QString hookCommand();
// Duet's plugin for its Claude sessions (--plugin-dir), or an empty string.
QString pluginPath();

// The duet-mcp program (the editor's MCP server), or an empty string.
QString mcpServerPath();
// JSON content of the --mcp-config file: the "duet" server run by
// `mcpServer`.
QByteArray mcpConfig(const QString &mcpServer);
// Where the --mcp-config file is written.
QString mcpConfigPath();

// What a session is started with, besides its folder and hooks.
struct SessionOptions {
    // After --settings ("--resume" and an identifier to resume a conversation).
    QStringList extraArguments;
    // With them, the session gets the editor's MCP server (duet-mcp), these
    // of its tools allowed without asking.
    std::optional<QStringList> mcpReadOnlyTools;
    // The name other Claude sessions address it by (--name); without one,
    // Claude Code names it after its folder.
    QString name;
    // Its identifier (--session-id, a UUID), which its hooks carry: tells
    // its events from the other sessions'. Without one, Claude Code picks it.
    QString sessionId;
    // Claude Code plugin folders loaded besides Duet's (--plugin-dir).
    QStringList pluginFolders;
};

// Process to launch, or nullopt (with `error` set) if `claude` or
// `duet-hook` cannot be found.
std::optional<ProcessSpec> claudeProcess(const QString &workingDirectory,
                                         const QString &hookServerName,
                                         QString *error,
                                         const SessionOptions &options = {});

// One-off `claude --print` run, outside the editor's session and without its
// hooks: `arguments` follow --print. The user's own login and settings apply,
// as for `claude` typed in a console. Nullopt (with `error` set) if `claude`
// cannot be found.
std::optional<ProcessSpec> printProcess(const QString &workingDirectory, const QStringList &arguments, QString *error);

} // namespace ClaudeLauncher
