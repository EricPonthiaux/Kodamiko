// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "lsp/LspProtocol.h"

#include <QHash>
#include <QList>
#include <QString>

class OccurrenceStore;

// After files moved or were renamed, the edits that make the project's
// references to them right again, as IntelliJ's Move does: C and C++
// quoted includes, JavaScript and TypeScript relative imports and
// requires, Python imports. A moved file's own references are written
// again from its new folder. Reads files: meant for a worker thread.
namespace FileReferences {

// A file that moves: either already moved (`to` exists), or about to move
// (`from` exists): the references are worked out the same, the texts read
// where the files are.
struct Move {
    QString from;
    QString to;

    bool operator==(const Move &) const = default;
};

struct Input {
    QList<Move> moves;
    QString projectFolder; // includes and Python modules are also looked up from it and its src/
    // Where to look for the references: the files the index says hold the
    // moved files' names, and the unsaved texts, read in their text (by path).
    const OccurrenceStore *index = nullptr;
    QHash<QString, QString> unsavedTexts;
};

// The files of `folder`'s move to `to`, made or to make: one Move each.
QList<Move> filesOf(const QString &folder, const QString &to);

LspProtocol::WorkspaceEdit update(const Input &input);

} // namespace FileReferences
