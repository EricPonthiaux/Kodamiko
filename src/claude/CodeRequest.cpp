// SPDX-License-Identifier: GPL-3.0-only

#include "CodeRequest.h"

#include "editor/EditorWidget.h"

#include <QDir>

namespace CodeRequest {

namespace {

QString relativePath(const QString &path, const QString &projectFolder)
{
    if (projectFolder.isEmpty())
        return path;
    const QString relative = QDir(projectFolder).relativeFilePath(path);
    return relative.startsWith(QLatin1String("..")) ? path : relative;
}

// What the request is about, in words: "`parse` at @a.cpp#L12" or the
// mention of the selected lines.
QString subject(const Target &target)
{
    if (target.symbol.isEmpty())
        return mention(target);
    return QStringLiteral("`%1` at %2").arg(target.symbol, mention(target));
}

// Claude reads the file from disk: the line numbers would not match.
QString unsavedNote(const Target &target)
{
    if (!target.unsaved)
        return {};
    return QStringLiteral(" The file has unsaved changes in the editor: the line numbers refer to its text "
                          "there, which Duet's get_file_text tool returns.");
}

} // namespace

std::optional<Target> targetIn(const EditorWidget &editor, const QString &projectFolder)
{
    if (editor.filePath().isEmpty())
        return std::nullopt;

    Target target;
    target.path = relativePath(editor.filePath(), projectFolder);
    target.unsaved = editor.isModified();
    if (editor.hasSelectedText()) {
        int lineFrom = 0;
        int indexFrom = 0;
        int lineTo = 0;
        int indexTo = 0;
        editor.getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);
        // Whole lines selected end at the start of the next one.
        if (indexTo == 0 && lineTo > lineFrom)
            --lineTo;
        target.firstLine = lineFrom + 1;
        target.lastLine = lineTo + 1;
        return target;
    }
    int line = 0;
    int index = 0;
    editor.getCursorPosition(&line, &index);
    target.firstLine = line + 1;
    target.lastLine = line + 1;
    target.symbol = editor.wordAtLineIndex(line, index);
    return target;
}

QString mention(const Target &target)
{
    // Claude Code ends a mention at a space: the path is quoted then.
    QString path = QDir::toNativeSeparators(target.path);
    if (path.contains(QLatin1Char(' ')))
        path = QStringLiteral("\"%1\"").arg(path);
    if (target.firstLine == target.lastLine)
        return QStringLiteral("@%1#L%2").arg(path).arg(target.firstLine);
    return QStringLiteral("@%1#L%2-%3").arg(path).arg(target.firstLine).arg(target.lastLine);
}

QString prompt(Kind kind, const Target &target)
{
    QString request;
    switch (kind) {
    case Kind::Explain:
        request = target.symbol.isEmpty()
                      ? QStringLiteral("Explain %1: what it does and why, briefly.")
                      : QStringLiteral("Explain %1: what it is, what it does and how the project uses it, briefly.");
        break;
    case Kind::Review:
        request = QStringLiteral("Review %1: point out problems with their line, then propose fixes, applying "
                                 "nothing until I agree.");
        break;
    case Kind::WriteTests:
        request = QStringLiteral("Write tests for %1, with the project's test framework and in the style of its "
                                 "existing tests, then run them.");
        break;
    }
    return request.arg(subject(target)) + unsavedNote(target);
}

} // namespace CodeRequest
