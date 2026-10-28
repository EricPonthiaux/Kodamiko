// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// The calls of Kodamiko's git tools (MCP) that do what a git command does, for
// GitCommandGuard. Only commands that change the repository have some, and
// only in the forms a tool does exactly: `git add -p`, `git push --force`
// or `git reset --hard` have none.
namespace GitCommandTranslations {

// A tool of Kodamiko's MCP server to call instead of a git command.
struct ToolCall {
    QString tool; // "stage_files"
    QJsonObject arguments;
    QString note; // what Claude fills in or should know; may be empty
};

// The tool calls doing what `git <subcommand> <arguments>` does, in order;
// empty when no tool does the same (or the command only reads).
QList<ToolCall> translate(const QString &subcommand, const QStringList &arguments);

} // namespace GitCommandTranslations
