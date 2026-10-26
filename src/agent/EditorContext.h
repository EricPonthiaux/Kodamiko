// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class EditorTabs;

// What the user has in front of them in the editor when they send a
// prompt, added to it as context (PromptSubmitted hook): the agent knows the
// file and the lines "this" refers to without an @path.
struct EditorContext {
    struct Problem {
        int line = 0; // 1-based
        bool error = true;
        QString message;
    };

    // An element selected in a diagram, named as its source names it.
    struct DiagramElement {
        QString key;  // "develop", or "develop -> main" for a link
        QString text; // the text it shows, empty for a link without label
    };

    QString filePath; // relative to the project when inside it
    int line = 0;     // cursor, 1-based; 0 without a file
    int column = 0;
    int selectionStartLine = 0; // 0 without a selection
    int selectionEndLine = 0;
    QString selectedText;
    QStringList unsavedFiles; // differ from the disk, relative paths
    QList<Problem> problems;  // of the current file
    // The diagram in the current tab, relative like filePath; empty when
    // the current tab is not a diagram. "This element" needs no question.
    QString diagramPath;
    QList<DiagramElement> diagramSelection; // in paint order, then the links
    // The files viewed before the current one, most recent first, with
    // the line of the cursor when they are open: "src/b.cpp:120". "The
    // function from before" needs no @path either.
    QStringList recentFiles;

    // Longest selection sent as is; a longer one is cut.
    static constexpr int maximumSelectionLength = 8000;
    static constexpr int maximumProblems = 20;
    static constexpr int maximumRecentFiles = 5;
    static constexpr int maximumDiagramSelection = 30;

    bool isEmpty() const
    {
        return filePath.isEmpty() && diagramPath.isEmpty() && unsavedFiles.isEmpty() && recentFiles.isEmpty();
    }

    // The text added to the prompt; empty when there is nothing to tell.
    QString describe() const;

    // What `tabs` show: the current editor's file, cursor, selection and
    // problems, or the current diagram and its selection, the unsaved files, and the files of `recentlyOpened`
    // (absolute paths, most recent first, see RecentFiles) still there;
    // paths relative to `projectFolder` when inside it.
    static EditorContext of(const EditorTabs &tabs,
                            const QString &projectFolder,
                            const QStringList &recentlyOpened = {});
    // As JSON, for the editor's MCP tools (the selection is not cut).
    QJsonObject toJson() const;
};
