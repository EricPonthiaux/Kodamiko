#pragma once

#include <QString>

// The folders the user trusts Claude Code in: before its first session in a
// folder, Claude asks whether to trust its files, and keeps the answer in
// its state file (ClaudeConfig::stateFile(), "projects" → the folder →
// "hasTrustDialogAccepted").
namespace ClaudeTrust {

// Whether Claude Code will start in `folder` without asking.
bool isTrusted(const QString &folder);

// Records that the user trusts `folder`, as if they had answered Claude's
// question: for a folder Duet created, whose files the user chose. Keeps
// the rest of the state file. Returns false with a message in `error` if
// the state file cannot be read as JSON or cannot be written; the file is
// then left as it was.
[[nodiscard]] bool trust(const QString &folder, QString *error);

} // namespace ClaudeTrust
