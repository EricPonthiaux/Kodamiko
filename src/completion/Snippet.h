#pragma once

#include <QString>

#include <QList>

// A language server snippet ("printValue(${1:int value})$0"), made into
// plain text: placeholders keep their text, tab stops and variables
// vanish, the first choice of a choice is kept. A tab stop repeated
// without text ("${1:i} < n; ++$1") repeats its placeholder's text.
struct ExpandedSnippet {
    QString text;
    // What to select after inserting: the first tab stop's placeholder
    // (empty at $1 with no text), else $0, else the end. In characters of
    // `text`.
    int selectionStart = 0;
    int selectionLength = 0;

    // In characters of `text`.
    struct Range {
        int start = 0;
        int length = 0;
    };
    // A tab stop and its occurrences: what is typed in one is copied to
    // the others (see SnippetSession).
    struct Stop {
        int index = 0; // $1, $2...; 0 for $0
        QList<Range> ranges;
    };
    // In the order Tab visits them: $1, $2... then $0, when there is one.
    QList<Stop> stops;
};

namespace Snippet {

ExpandedSnippet expand(const QString &snippet);

} // namespace Snippet
