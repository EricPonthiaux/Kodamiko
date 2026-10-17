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
// editor's read-only MCP tools).
QByteArray sessionSettings(const QString &hookCommand, const QStringList &allowedTools = {});
// Where the --settings file is written.
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

// Process to launch, or nullopt (with `error` set) if `claude` or
// `duet-hook` cannot be found. `extraArguments` follow --settings
// ("--continue" to resume the last conversation). With `mcpReadOnlyTools`,
// the session gets the editor's MCP server (duet-mcp), those of its tools
// allowed without asking.
std::optional<ProcessSpec> claudeProcess(const QString &workingDirectory, const QString &hookServerName,
                                         QString *error, const QStringList &extraArguments = {},
                                         const std::optional<QStringList> &mcpReadOnlyTools = std::nullopt);

// One-off `claude --print` run, outside the editor's session and without its
// hooks: `arguments` follow --print. The user's own login and settings apply,
// as for `claude` typed in a console. Nullopt (with `error` set) if `claude`
// cannot be found.
std::optional<ProcessSpec> printProcess(const QString &workingDirectory, const QStringList &arguments,
                                        QString *error);

} // namespace ClaudeLauncher
