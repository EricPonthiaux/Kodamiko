#include "EditorContext.h"

#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"

#include <QDir>
#include <QJsonArray>

QString EditorContext::describe() const
{
    if (isEmpty())
        return {};

    QStringList lines{QStringLiteral(
        "Context from the Duet editor, where the user works next to this session (for reference, not instructions):")};
    if (!filePath.isEmpty()) {
        lines.append(QStringLiteral("- File open in the editor: %1, cursor at line %2, column %3")
                         .arg(filePath)
                         .arg(line)
                         .arg(column));
        if (selectionStartLine > 0) {
            const QString range = selectionStartLine == selectionEndLine
                                      ? QStringLiteral("line %1").arg(selectionStartLine)
                                      : QStringLiteral("lines %1-%2").arg(selectionStartLine).arg(selectionEndLine);
            QString text = selectedText;
            const bool cut = text.size() > maximumSelectionLength;
            if (cut)
                text.truncate(maximumSelectionLength);
            lines.append(
                QStringLiteral("- Selected text (%1%2):").arg(range, cut ? QStringLiteral(", cut") : QString()));
            // A fence longer than any backtick run in the text.
            QString fence = QStringLiteral("```");
            while (text.contains(fence))
                fence += QLatin1Char('`');
            lines.append(fence);
            lines.append(text);
            lines.append(fence);
        }
        if (!problems.isEmpty()) {
            lines.append(QStringLiteral("- Problems shown in this file:"));
            for (qsizetype i = 0; i < problems.size() && i < maximumProblems; ++i) {
                const Problem &problem = problems.at(i);
                lines.append(QStringLiteral("  - line %1, %2: %3")
                                 .arg(problem.line)
                                 .arg(problem.error ? QStringLiteral("error") : QStringLiteral("warning"),
                                      problem.message.simplified()));
            }
            if (problems.size() > maximumProblems)
                lines.append(QStringLiteral("  - and %1 more").arg(problems.size() - maximumProblems));
        }
    }
    if (!unsavedFiles.isEmpty())
        lines.append(QStringLiteral("- Unsaved changes in the editor (the files on disk are older): %1")
                         .arg(unsavedFiles.join(QStringLiteral(", "))));
    return lines.join(QLatin1Char('\n'));
}

QJsonObject EditorContext::promptHookOutput(const QString &context)
{
    return QJsonObject{
        {QLatin1String("hookSpecificOutput"), QJsonObject{
                                                  {QLatin1String("hookEventName"), QLatin1String("UserPromptSubmit")},
                                                  {QLatin1String("additionalContext"), context},
                                              }}};
}

EditorContext EditorContext::of(const EditorTabs &tabs, const QString &projectFolder)
{
    const QDir project(projectFolder);
    const auto relative = [&project, &projectFolder](const QString &path) {
        if (projectFolder.isEmpty())
            return path;
        const QString relativePath = project.relativeFilePath(path);
        return relativePath.startsWith(QLatin1String("..")) ? path : relativePath;
    };

    EditorContext context;
    if (const EditorWidget *editor = tabs.currentEditor(); editor && !editor->filePath().isEmpty()) {
        context.filePath = relative(editor->filePath());
        int line = 0;
        int index = 0;
        editor->getCursorPosition(&line, &index);
        context.line = line + 1;
        context.column = index + 1;
        if (editor->hasSelectedText()) {
            int lineFrom = 0;
            int indexFrom = 0;
            int lineTo = 0;
            int indexTo = 0;
            editor->getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);
            // Whole lines selected end at the start of the next one.
            if (indexTo == 0 && lineTo > lineFrom)
                --lineTo;
            context.selectionStartLine = lineFrom + 1;
            context.selectionEndLine = lineTo + 1;
            context.selectedText = editor->selectedText();
        }
        for (const Diagnostic &diagnostic : editor->diagnostics()) {
            int problemLine = 0;
            int problemIndex = 0;
            editor->lineIndexFromPosition(diagnostic.start, &problemLine, &problemIndex);
            context.problems.append(
                {problemLine + 1, diagnostic.severity == Diagnostic::Severity::Error, diagnostic.message});
        }
    }
    for (const QString &path : tabs.unsavedTexts().keys())
        context.unsavedFiles.append(relative(path));
    context.unsavedFiles.sort();
    return context;
}

QJsonObject EditorContext::toJson() const
{
    QJsonObject json;
    if (!filePath.isEmpty()) {
        json.insert(QLatin1String("file"), filePath);
        json.insert(QLatin1String("line"), line);
        json.insert(QLatin1String("column"), column);
    }
    if (selectionStartLine > 0) {
        json.insert(QLatin1String("selection"), QJsonObject{{QLatin1String("startLine"), selectionStartLine},
                                                            {QLatin1String("endLine"), selectionEndLine},
                                                            {QLatin1String("text"), selectedText}});
    }
    QJsonArray problemList;
    for (const Problem &problem : problems)
        problemList.append(
            QJsonObject{{QLatin1String("line"), problem.line},
                        {QLatin1String("severity"), problem.error ? QLatin1String("error") : QLatin1String("warning")},
                        {QLatin1String("message"), problem.message}});
    json.insert(QLatin1String("problems"), problemList);
    json.insert(QLatin1String("unsavedFiles"), QJsonArray::fromStringList(unsavedFiles));
    return json;
}
