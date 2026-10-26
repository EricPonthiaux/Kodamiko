// SPDX-License-Identifier: GPL-3.0-only

#include "EditorContext.h"

#include "editor/EditorTabs.h"
#include "editor/EditorWidget.h"

#ifdef DUET_WITH_DIAGRAMS
#include "viewers/DiagramView.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Shape.h"
#endif

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

namespace {

// A fence longer than any backtick run in `text`.
QString fenceFor(const QString &text)
{
    QString fence = QStringLiteral("```");
    while (text.contains(fence))
        fence += QLatin1Char('`');
    return fence;
}

QStringList describeSelection(const EditorContext &context)
{
    const QString range =
        context.selectionStartLine == context.selectionEndLine
            ? QStringLiteral("line %1").arg(context.selectionStartLine)
            : QStringLiteral("lines %1-%2").arg(context.selectionStartLine).arg(context.selectionEndLine);
    QString text = context.selectedText;
    const bool cut = text.size() > EditorContext::maximumSelectionLength;
    if (cut)
        text.truncate(EditorContext::maximumSelectionLength);
    const QString fence = fenceFor(text);
    return {QStringLiteral("- Selected text (%1%2):").arg(range, cut ? QStringLiteral(", cut") : QString()), fence,
            text, fence};
}

QStringList describeProblems(const QList<EditorContext::Problem> &problems)
{
    QStringList lines{QStringLiteral("- Problems shown in this file:")};
    for (qsizetype i = 0; i < problems.size() && i < EditorContext::maximumProblems; ++i) {
        const EditorContext::Problem &problem = problems.at(i);
        lines.append(QStringLiteral("  - line %1, %2: %3")
                         .arg(problem.line)
                         .arg(problem.error ? QStringLiteral("error") : QStringLiteral("warning"),
                              problem.message.simplified()));
    }
    if (problems.size() > EditorContext::maximumProblems)
        lines.append(QStringLiteral("  - and %1 more").arg(problems.size() - EditorContext::maximumProblems));
    return lines;
}

QStringList describeDiagramSelection(const QList<EditorContext::DiagramElement> &elements)
{
    QStringList lines{QStringLiteral("- Selected in the diagram (key: text):")};
    for (qsizetype i = 0; i < elements.size() && i < EditorContext::maximumDiagramSelection; ++i) {
        const EditorContext::DiagramElement &element = elements.at(i);
        const QString text = element.text.simplified();
        lines.append(text.isEmpty() ? QStringLiteral("  - %1").arg(element.key)
                                    : QStringLiteral("  - %1: %2").arg(element.key, text));
    }
    if (elements.size() > EditorContext::maximumDiagramSelection)
        lines.append(QStringLiteral("  - and %1 more").arg(elements.size() - EditorContext::maximumDiagramSelection));
    return lines;
}

#ifdef DUET_WITH_DIAGRAMS
// How the source names a shape: its key, else its id (as the diagram MCP tools do).
QString keyOf(const diagram::Shape &shape)
{
    return shape.key.isEmpty() ? shape.id : shape.key;
}

QList<EditorContext::DiagramElement> selectionOf(const diagram::Board &board)
{
    QList<EditorContext::DiagramElement> elements;
    for (const auto &shape : board.shapes()) {
        if (board.selection().contains(shape->id))
            elements.append({keyOf(*shape), shape->displayText()});
    }
    const auto endKey = [&board](const QString &id) {
        const diagram::Shape *shape = board.shape(id);
        return shape ? keyOf(*shape) : id;
    };
    for (const auto &link : board.links()) {
        if (board.linkSelection().contains(link->id))
            elements.append({QStringLiteral("%1 -> %2").arg(endKey(link->from), endKey(link->to)), link->label});
    }
    return elements;
}
#endif

// "src/b.cpp:120" for an open file, "src/b.cpp" for one no longer open.
QString describeRecentFile(const EditorTabs &tabs, const QString &path, const QString &relativePath)
{
    const EditorWidget *editor = tabs.editorFor(path);
    if (!editor)
        return relativePath;
    int line = 0;
    int index = 0;
    editor->getCursorPosition(&line, &index);
    return QStringLiteral("%1:%2").arg(relativePath).arg(line + 1);
}

} // namespace

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
        if (selectionStartLine > 0)
            lines += describeSelection(*this);
        if (!problems.isEmpty())
            lines += describeProblems(problems);
    }
    if (!diagramPath.isEmpty()) {
        lines.append(QStringLiteral("- Diagram open in the editor: %1").arg(diagramPath));
        if (!diagramSelection.isEmpty())
            lines += describeDiagramSelection(diagramSelection);
    }
    if (!recentFiles.isEmpty())
        lines.append(
            QStringLiteral("- Files viewed before, most recent first: %1").arg(recentFiles.join(QStringLiteral(", "))));
    if (!unsavedFiles.isEmpty())
        lines.append(QStringLiteral("- Unsaved changes in the editor (the files on disk are older): %1")
                         .arg(unsavedFiles.join(QStringLiteral(", "))));
    return lines.join(QLatin1Char('\n'));
}

EditorContext EditorContext::of(const EditorTabs &tabs, const QString &projectFolder, const QStringList &recentlyOpened)
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
#ifdef DUET_WITH_DIAGRAMS
    const DiagramView *diagram = tabs.currentEditor() ? nullptr : tabs.diagramViewFor(tabs.currentFilePath());
    if (diagram) {
        context.diagramPath = relative(diagram->filePath());
        context.diagramSelection = selectionOf(*diagram->board());
    }
#endif
    const QString currentPath = tabs.currentFilePath();
    for (const QString &path : recentlyOpened) {
        if (context.recentFiles.size() == maximumRecentFiles)
            break;
        const bool isCurrent = !currentPath.isEmpty() && currentPath == path;
        if (!isCurrent && QFileInfo::exists(path))
            context.recentFiles.append(describeRecentFile(tabs, path, relative(path)));
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
    if (!diagramPath.isEmpty()) {
        QJsonArray selection;
        for (const DiagramElement &element : diagramSelection)
            selection.append(QJsonObject{{QLatin1String("key"), element.key}, {QLatin1String("text"), element.text}});
        json.insert(QLatin1String("diagram"),
                    QJsonObject{{QLatin1String("file"), diagramPath}, {QLatin1String("selection"), selection}});
    }
    QJsonArray problemList;
    for (const Problem &problem : problems)
        problemList.append(
            QJsonObject{{QLatin1String("line"), problem.line},
                        {QLatin1String("severity"), problem.error ? QLatin1String("error") : QLatin1String("warning")},
                        {QLatin1String("message"), problem.message}});
    json.insert(QLatin1String("problems"), problemList);
    json.insert(QLatin1String("recentFiles"), QJsonArray::fromStringList(recentFiles));
    json.insert(QLatin1String("unsavedFiles"), QJsonArray::fromStringList(unsavedFiles));
    return json;
}
