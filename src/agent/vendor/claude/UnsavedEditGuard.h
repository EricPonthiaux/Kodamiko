// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QStringList>

// Keeps Claude from overwriting what the user has typed but not saved: an
// editing tool on a file with unsaved changes in the editor is refused
// (PreToolUse hook), with the reason for Claude, which asks the user to
// save or discard them first.
namespace UnsavedEditGuard {

// The reason given to Claude for `files` (paths relative to the project).
QString reason(const QStringList &files);

} // namespace UnsavedEditGuard
