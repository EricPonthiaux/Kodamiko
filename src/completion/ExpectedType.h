#pragma once

#include "Completion.h"

#include <QByteArray>
#include <QString>

// Smart completion (Ctrl+Shift+Space): the type the code expects at the
// cursor, and the candidates that have it.
namespace ExpectedType {

// The type expected at `position` (bytes of `text`), as far as the text
// tells: the declared type in "Widget *w = |", the variable's in
// "count = |", the function's in "return |", "bool" in "if (|". Empty
// when unknown (a call's argument: the language server's signature help
// knows it).
QString at(const QByteArray &text, int position, const QString &languageId);

// The type of a parameter's label: "const QString &name" → "const QString
// &", "name: string" → "string", "int" → "int".
QString ofParameter(const QString &label, const QString &languageId);

// The type of a candidate's value from a language server's detail:
// "int", "Main.count : int", "(x: int) -> str".
QString ofDetail(const QString &detail);

// The type without qualifiers, references or spaces: "const std::string &"
// → "std::string".
QString normalized(const QString &type);

// Whether `item` fits where `expected` is: its type is the same (or of
// the same family: numbers, strings, booleans), or it is the class to
// create after "new". With no expected type, any value.
bool accepts(const QString &expected, const CompletionItem &item, bool afterNew = false);

} // namespace ExpectedType
