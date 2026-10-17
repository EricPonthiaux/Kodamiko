#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// What the user has in front of them in the editor when they send a
// prompt, added to it as context (UserPromptSubmit hook): Claude knows the
// file and the lines "this" refers to without an @path.
struct EditorContext {
    struct Problem {
        int line = 0; // 1-based
        bool error = true;
        QString message;
    };

    QString filePath; // relative to the project when inside it
    int line = 0;     // cursor, 1-based; 0 without a file
    int column = 0;
    int selectionStartLine = 0; // 0 without a selection
    int selectionEndLine = 0;
    QString selectedText;
    QStringList unsavedFiles; // differ from the disk, relative paths
    QList<Problem> problems;  // of the current file

    // Longest selection sent as is; a longer one is cut.
    static constexpr int maximumSelectionLength = 8000;
    static constexpr int maximumProblems = 20;

    bool isEmpty() const { return filePath.isEmpty() && unsavedFiles.isEmpty(); }

    // The text added to the prompt; empty when there is nothing to tell.
    QString describe() const;

    // The UserPromptSubmit hook's output adding `context` to the prompt.
    static QJsonObject promptHookOutput(const QString &context);
};
