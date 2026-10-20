// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

#include <optional>

class EditorWidget;

// Questions about the code in front of the user, typed in Claude's prompt
// by the editor's context menu and the Claude menu. They name the code with
// a file mention and its lines, so they hold even when the editor context
// is not shared with each prompt.
namespace CodeRequest {

enum class Kind {
    Explain,
    Review,
    WriteTests,
};

// The code a request is about: the selected lines or, without a
// selection, the cursor's line and the word under it.
struct Target {
    QString path;      // relative to the project when inside it
    int firstLine = 0; // 1-based
    int lastLine = 0;
    QString symbol;       // the word at the cursor; empty with a selection
    bool unsaved = false; // the editor's text differs from the file
};

// The target in `editor`, its path relative to `projectFolder` when inside
// it; nullopt for a view that shows no file (a diff…).
std::optional<Target> targetIn(const EditorWidget &editor, const QString &projectFolder);

// "@src/main.cpp#L10-24", the file mention with lines Claude Code reads.
QString mention(const Target &target);

// The prompt asking Claude for `kind` about `target`.
QString prompt(Kind kind, const Target &target);

} // namespace CodeRequest
