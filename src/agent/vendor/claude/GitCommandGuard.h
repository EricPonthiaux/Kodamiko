// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

#include <optional>

// Keeps Claude from changing the project's repository with git in a shell
// when one of Kodamiko's git tools (MCP) does the same: the tool asks the user
// with a plain description, takes a snapshot before what is hard to undo,
// runs one git operation at a time with the editor's, asks for credentials
// in the editor, and keeps Kodamiko's views in step. Such a command is refused
// (PreToolUse hook on Bash) with the tool calls that do its work, their
// arguments included, so that Claude goes on in one step.
//
// Only what changes the repository and has a tool is refused. Reading git
// (status, log, diff, show, blame…), commands without an equivalent tool
// (push --force, reset --hard, tag…) and git run in another repository
// (`cd elsewhere && git …`, `git -C elsewhere …`) are left alone.
namespace GitCommandGuard {

// Where a command runs.
struct Location {
    QString projectFolder;    // the project Kodamiko's tools act on
    QString workingDirectory; // the shell's current directory; empty: the project
};

// The reason to refuse `commandLine` (a shell command line), listing for
// each git command it covers the tool call to make instead; nullopt when
// it runs none in the project.
std::optional<QString> refusal(const QString &commandLine, const Location &location);

} // namespace GitCommandGuard
