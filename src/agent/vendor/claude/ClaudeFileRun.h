// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "agent/AgentProvider.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// Claude Code's runs without a session, one-off `claude --print` processes:
// file runs (AgentFileRun), with Duet's plugin (the diagrams skill), allowed
// to read the project and to write the run's file only; review runs
// (AgentReviewRun), allowed to read the project only. Their stream-json
// output tells what Claude is doing, for the loading screen.
namespace ClaudeFileRun {

// The arguments that follow --print. `hookCommand`, when given, checks the
// file after each write (PostToolUse hook, see DiagramCheck); `pluginPath`
// provides the diagrams skill.
QStringList arguments(const QString &prompt,
                      const QString &relativePath,
                      const QString &pluginPath,
                      const QString &hookCommand);

// The arguments that follow --print for `run`.
QStringList reviewArguments(const AgentReviewRun &run);

// One line of the stream-json output; file paths in steps are shown
// relative to `workDir`.
AgentRunEvent parseLine(const QByteArray &line, const QString &workDir);

} // namespace ClaudeFileRun
