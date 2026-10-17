#include "EditorContext.h"

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
            lines.append(QStringLiteral("- Selected text (%1%2):").arg(range, cut ? QStringLiteral(", cut") : QString()));
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
    return QJsonObject{{QLatin1String("hookSpecificOutput"),
                        QJsonObject{
                            {QLatin1String("hookEventName"), QLatin1String("UserPromptSubmit")},
                            {QLatin1String("additionalContext"), context},
                        }}};
}
