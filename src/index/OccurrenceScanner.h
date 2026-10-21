// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Occurrence.h"

#include <QList>
#include <QString>

#include <tree_sitter/api.h>

struct OccurrenceGrammar;

// Finds the words of a file. Code in a language of OccurrenceGrammars is
// parsed: each identifier is an occurrence, with the role the language's
// query gives it, and the words of its comments and strings are comment
// and string occurrences. The words of any other text are Text
// occurrences. A scanner keeps a parser: one per thread.
class OccurrenceScanner {
public:
    OccurrenceScanner();
    ~OccurrenceScanner();

    OccurrenceScanner(const OccurrenceScanner &) = delete;
    OccurrenceScanner &operator=(const OccurrenceScanner &) = delete;

    // The occurrences of `text` (UTF-8), in text order. Comment, string
    // and text words have two characters at least.
    QList<Occurrence> scan(const QString &languageId, const QByteArray &text);

private:
    QList<Occurrence> scanCode(const OccurrenceGrammar &grammar, const QByteArray &text);

    TSParser *parser_;
    TSQueryCursor *cursor_;
};
