// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "lsp/LspProtocol.h"

#include <QHash>
#include <QString>
#include <QStringList>

class OccurrenceStore;

// What a rename changes besides the code the language server renames, and
// what could go wrong, as IntelliJ's rename tells: the name in comments,
// strings and files that are not code, and the conflicts. Reads files:
// meant for a worker thread.
namespace RenameAnalysis {

struct Input {
    QString name;
    QString newName;
    LspProtocol::WorkspaceEdit codeEdit; // the server's
    // Where the name occurs: the files the index says, and the open ones
    // with unsaved changes, read in their text (by path).
    const OccurrenceStore *index = nullptr;
    QHash<QString, QString> unsavedTexts;
    QString projectFolder;
    bool searchesCommentsAndStrings = true;
};

struct Result {
    // The name in comments, strings and non-code files, replaced; none of
    // these edits overlaps the server's.
    LspProtocol::WorkspaceEdit textualEdit;
    // One sentence each: the new name already declared in a file the
    // rename changes, a read-only file, a file outside the project.
    QStringList conflicts;
};

Result analyze(const Input &input);

// The UTF-16 position of byte `offset` on `line` of `text`, whose line
// starts at byte `lineStart`. Public for tests.
LspProtocol::Position positionOf(const QByteArray &text, qsizetype lineStart, std::uint32_t offset, int line);

} // namespace RenameAnalysis
