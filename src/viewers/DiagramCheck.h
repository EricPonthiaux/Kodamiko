// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>

// Checks a diagram file the way the diagram editor reads it, for Claude: what
// it could not read, or had to leave out. Claude gets the problems right
// after writing the file (PostToolUse hook) and fixes them.
namespace DiagramCheck {

// The problems of a diagram file's content; empty if it reads as written.
QStringList problems(const QString &path, const QByteArray &content);

// The PostToolUse hook's output reporting `problems` of `file` to Claude.
QJsonObject feedbackOutput(const QString &file, const QStringList &problems);

} // namespace DiagramCheck
