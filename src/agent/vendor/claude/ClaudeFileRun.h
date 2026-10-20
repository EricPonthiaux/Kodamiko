// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "agent/AgentProvider.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// Claude Code's file runs (AgentFileRun): a one-off `claude --print` with
// Duet's plugin (the diagrams skill), allowed to read the project and to
// write the run's file only. Its stream-json output tells what Claude is
// doing, for the loading screen.
namespace ClaudeFileRun {

// The arguments that follow --print. `hookCommand`, when given, checks the
// file after each write (PostToolUse hook, see DiagramCheck); `pluginPath`
// provides the diagrams skill.
QStringList arguments(const QString &prompt,
                      const QString &relativePath,
                      const QString &pluginPath,
                      const QString &hookCommand);

// One line of the stream-json output; file paths in steps are shown
// relative to `workDir`.
AgentRunEvent parseLine(const QByteArray &line, const QString &workDir);

} // namespace ClaudeFileRun
