// SPDX-License-Identifier: GPL-3.0-only

#include "CodeRequest.h"

#include "editor/EditorWidget.h"

#include <QDir>
#include <QFileInfo>

namespace CodeRequest {

namespace {

QString relativePath(const QString &path, const QString &projectFolder)
{
    if (projectFolder.isEmpty())
        return path;
    const QString relative = QDir(projectFolder).relativeFilePath(path);
    return relative.startsWith(QLatin1String("..")) ? path : relative;
}

// Claude Code ends a mention at a space: the path is quoted then.
QString mentionedPath(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
    return native.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(native) : native;
}

// "src/" for a folder, "src/main.cpp" for a file.
QString entryName(const QString &path, const QString &projectFolder)
{
    QString relative = relativePath(path, projectFolder);
    if (relative == QLatin1String("."))
        relative = QStringLiteral("./");
    else if (QFileInfo(path).isDir())
        relative += QLatin1Char('/');
    return QDir::toNativeSeparators(relative);
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

// "error at @src/a.cpp#L42: use of undeclared identifier 'x' (clangd)"
QString describeProblem(const Problem &problem, const QString &projectFolder)
{
    const Target at{.path = relativePath(problem.path, projectFolder),
                    .firstLine = problem.line + 1,
                    .lastLine = problem.line + 1,
                    .symbol = {},
                    .unsaved = false};
    QString text =
        QStringLiteral("%1 at %2: %3")
            .arg(problem.severity == Diagnostic::Severity::Error ? QStringLiteral("error") : QStringLiteral("warning"),
                 mention(at), problem.message.simplified());
    if (!problem.source.isEmpty())
        text += QStringLiteral(" (%1)").arg(problem.source);
    return text;
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
    const QString path = mentionedPath(target.path);
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
    case Kind::WriteTests:
        request = QStringLiteral("Write tests for %1, with the project's test framework and in the style of its "
                                 "existing tests, then run them.");
        break;
    }
    return request.arg(subject(target)) + unsavedNote(target);
}

QString fixPrompt(const Problems &problems, const QString &projectFolder, bool unsaved)
{
    Q_ASSERT(!problems.isEmpty());
    QStringList lines;
    if (problems.size() == 1) {
        lines.append(QStringLiteral("Fix this %1.").arg(describeProblem(problems.first(), projectFolder)));
    } else {
        lines.append(QStringLiteral("Fix these problems:"));
        for (qsizetype i = 0; i < problems.size() && i < maximumProblemsToFix; ++i)
            lines.append(QStringLiteral("- %1").arg(describeProblem(problems.at(i), projectFolder)));
        if (problems.size() > maximumProblemsToFix)
            lines.append(QStringLiteral("- and %1 more").arg(problems.size() - maximumProblemsToFix));
    }
    // Claude reads the files from disk: the line numbers would not match.
    if (unsaved)
        lines.append(QStringLiteral("The files have unsaved changes in the editor: the line numbers refer to their "
                                    "text there, which Duet's get_file_text tool returns."));
    return lines.join(QLatin1Char('\n'));
}

QString describe(const Target &target)
{
    const QString path = QDir::toNativeSeparators(target.path);
    QString lines;
    if (target.firstLine == target.lastLine)
        lines = QStringLiteral("%1, line %2").arg(path).arg(target.firstLine);
    else
        lines = QStringLiteral("%1, lines %2-%3").arg(path).arg(target.firstLine).arg(target.lastLine);
    if (target.symbol.isEmpty())
        return lines;
    return QStringLiteral("`%1` in %2").arg(target.symbol, lines);
}

QString describe(const QStringList &paths, const QString &projectFolder)
{
    QStringList names;
    for (const QString &path : paths)
        names.append(entryName(path, projectFolder));
    return names.join(QStringLiteral(", "));
}

} // namespace CodeRequest
