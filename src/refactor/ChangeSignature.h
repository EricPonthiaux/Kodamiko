// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "lsp/LspProtocol.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Change Signature, as IntelliJ's: the parameters of a function added,
// removed or reordered, in its declarations and at each call, worked out
// from the texts alone (nothing is read or written). The places come from
// the language server's references; each one's parameter or argument list
// is found in its syntax tree and written again. For C, C++, Java,
// JavaScript, TypeScript and Python; in Python, "self" and "cls" stay
// first and the keyword arguments follow their parameters.
namespace ChangeSignature {

// A function's signature where the cursor is.
struct Signature {
    QString name;
    QStringList parameters; // their declarations ("int count = 0")
    QStringList names;      // their names ("count")
    QString error;          // why none was found
};

Signature read(const QString &languageId, const QString &text, const LspProtocol::Position &cursor);
// The name a parameter's `declaration` gives ("count" of "int count = 0"),
// empty if it gives none.
QString nameOf(const QString &languageId, const QString &declaration);

// A parameter of the new signature.
struct Parameter {
    std::optional<int> oldIndex; // a parameter kept, from the old ones
    QString declaration;         // a new one's ("int count", "count: number")
    QString name;                // a new one's, for Python keyword arguments
    QString valueAtCalls;        // what the calls pass to a new one
};

struct Input {
    QList<Parameter> parameters; // in their new order
    QStringList oldNames;        // Signature::names
    // Where the function is named: its declarations, definitions and calls.
    QList<LspProtocol::Location> places;
    // The files' texts, by path (the unsaved ones included).
    QHash<QString, QString> texts;
};

struct Result {
    LspProtocol::WorkspaceEdit edit;
    // A place that is neither a call nor a declaration (the function
    // passed as a value), or a call leaving out an argument that would be
    // needed: left as it is, one sentence each.
    QStringList conflicts;
};

Result plan(const Input &input);

} // namespace ChangeSignature
