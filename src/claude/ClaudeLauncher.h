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
// `hookCommand statusline`.
QByteArray sessionSettings(const QString &hookCommand);
// Where the --settings file is written.
QString sessionSettingsPath();

// Status line command configured by the user for `projectDir` (user,
// project and local settings), or an empty string.
QString userStatusLineCommand(const QString &projectDir);

// The `claude` program found, or an empty string.
QString findClaudeExecutable();

// Process to launch, or nullopt (with `error` set) if `claude` or
// `duet-hook` cannot be found. `extraArguments` follow --settings
// ("--continue" to resume the last conversation).
std::optional<ProcessSpec> claudeProcess(const QString &workingDirectory, const QString &hookServerName,
                                         QString *error, const QStringList &extraArguments = {});

} // namespace ClaudeLauncher
