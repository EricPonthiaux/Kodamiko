// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "diagnostics/Diagnostic.h"

#include <QByteArrayView>
#include <QString>

// Checks of Claude Code's configuration files as they are edited:
// settings files against SchemaStore's schema, and the front matter of
// skills (SKILL.md) and agents.
namespace ClaudeLint {

enum class Kind {
    None,
    Settings,
    Skill,
    Agent
};

// What `path` is to Claude Code: settings.json or settings.local.json in a
// .claude folder or Claude's configuration folder, managed-settings.json,
// a SKILL.md in a skills/ folder, a Markdown file in an agents/ folder.
Kind kindOf(const QString &path);

Diagnostics check(Kind kind, QByteArrayView text);

// For a settings file: the schema's description of the key at byte
// `position`, or an empty string.
QString settingsHelp(QByteArrayView text, int position);

} // namespace ClaudeLint
