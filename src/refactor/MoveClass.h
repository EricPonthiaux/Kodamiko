// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "lsp/LspProtocol.h"

#include <QString>

// Move Class to Its Own File, for C++ and Java, worked out from the texts
// alone (nothing is read or written):
// - C++: the class at the cursor goes to a header named after it, next to
//   its file, with the file's leading comment (its license), its include
//   guard style, its includes and the class's namespaces; the file
//   includes the new header in its place. The definitions of its members
//   in the file's source go to a source named after the class too.
// - Java: a top-level class that is not the file's public one goes to a
//   file named after it, with the file's package and imports.
// The includes and imports are copied whole: some may be left unused.
namespace MoveClass {

struct Input {
    QString path;
    QString languageId; // "cpp" or "java"
    QString text;       // as in the editor
    LspProtocol::Position cursor;
    // C++: the source holding the members' definitions, if any.
    QString sourcePath;
    QString sourceText;
};

struct Result {
    QString className;
    QString newPath; // the new header, or Java file
    LspProtocol::WorkspaceEdit edit;
    QString error; // why the class cannot move; the edit is then empty
};

Result plan(const Input &input);

} // namespace MoveClass
