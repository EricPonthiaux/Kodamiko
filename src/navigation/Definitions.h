// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "SymbolIndex.h"

// Choice of the "Go to Definition" target among symbols with the same
// name. Without semantic analysis, it is a heuristic: types before
// functions, definitions before prototypes, the current method's class
// before the others.
namespace Definitions {

// The word under the cursor and its context.
struct Request {
    QString word;
    QString qualifier;         // "Foo" in "Foo::bar", otherwise empty
    bool memberAccess = false; // preceded by "." or "->"
    QString filePath;
    QString languageId;
    int position = 0; // start of the word, in bytes
    int line = 0;
};

struct Result {
    QList<SymbolLocation> targets; // most likely first
    bool ambiguous = false;        // several equally likely targets: let the user choose
};

// `matches`: project symbols named `request.word`; `fileSymbols`:
// symbols of the file containing the cursor. If the cursor is already on a
// definition, the targets are its declarations (and vice versa).
Result resolve(const Request &request, QList<SymbolLocation> matches, const QList<SymbolLocation> &fileSymbols);

} // namespace Definitions
