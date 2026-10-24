// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QStringList>

#include <optional>

class EditorWidget;

// Questions about the code in front of the user, typed in Claude's prompt
// by the editor's context menu and the Claude menu. They name the code with
// a file mention and its lines, so they hold even when the editor context
// is not shared with each prompt.
//
// Reviews are not typed in the session: they run apart (AgentReviewRun, see
// ReviewRequest), so that the reviewer does not inherit the reasoning that
// wrote the code; describe() names their subject.
namespace CodeRequest {

enum class Kind {
    Explain,
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

// What a review is about, in words, for its prompt and its tab:
// "src/a.cpp, lines 2-3", "`parse` in a.cpp, line 1".
QString describe(const Target &target);
// The same for whole files and folders (absolute paths, from the file
// tree), named relative to `projectFolder` when inside it: "src/, a.cpp".
QString describe(const QStringList &paths, const QString &projectFolder);

} // namespace CodeRequest
