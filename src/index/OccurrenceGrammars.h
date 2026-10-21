// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

#include <tree_sitter/api.h>

class HighlightQuery;

// A language the occurrence index parses, with its occurrences.scm query.
struct OccurrenceGrammar {
    const TSLanguage *language;
    const HighlightQuery &query;
};

namespace OccurrenceGrammars {

// The grammar of `languageId` ("cpp", "python"…), nullptr for a language
// the index reads as plain text. The query is compiled on the first call
// and kept until the program ends. Thread-safe.
const OccurrenceGrammar *forLanguage(const QString &languageId);

} // namespace OccurrenceGrammars
