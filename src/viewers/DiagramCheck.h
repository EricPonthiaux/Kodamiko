// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Checks a diagram file the way the diagram editor reads it, for the agent: what
// it could not read, or had to leave out. The agent gets the problems right
// after writing the file (PostToolUse hook) and fixes them.
namespace DiagramCheck {

// The problems of a diagram file's content; empty if it reads as written.
QStringList problems(const QString &path, const QByteArray &content);

// What the agent is told about the `problems` of `file`, to fix them
// (AgentProvider::blockReply).
QString feedback(const QString &file, const QStringList &problems);

} // namespace DiagramCheck
