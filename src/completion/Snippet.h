#pragma once

#include <QString>

// A language server snippet ("printValue(${1:int value})$0"), made into
// plain text: placeholders keep their text, tab stops and variables
// vanish, the first choice of a choice is kept.
struct ExpandedSnippet {
    QString text;
    // What to select after inserting: the first tab stop's placeholder
    // (empty at $1 with no text), else $0, else the end. In characters of
    // `text`.
    int selectionStart = 0;
    int selectionLength = 0;
};

namespace Snippet {

ExpandedSnippet expand(const QString &snippet);

} // namespace Snippet
