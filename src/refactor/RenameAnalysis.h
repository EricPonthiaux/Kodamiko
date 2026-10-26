// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "lsp/LspProtocol.h"

#include <QHash>
#include <QString>
#include <QStringList>

// What a rename changes besides the code the language server renames, and
// what could go wrong, as IntelliJ's rename tells: the name in the comments
// and strings of the files the server's rename changes (nowhere else: a
// word of another file or of a stylesheet is not the symbol), and the
// conflicts. Reads files: meant for a worker thread.
namespace RenameAnalysis {

struct Input {
    QString name;
    QString newName;
    LspProtocol::WorkspaceEdit codeEdit; // the server's
    // The open files with unsaved changes, read in their text (by path).
    QHash<QString, QString> unsavedTexts;
    QString projectFolder;
    bool searchesCommentsAndStrings = true;
};

struct Result {
    // The name in the comments and strings of the server's files,
    // replaced; none of these edits overlaps the server's.
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
