#pragma once

#include <QString>

#include <tree_sitter/api.h>

class HighlightQuery;

struct TreeSitterGrammar {
    const TSLanguage *language;
    const HighlightQuery &highlights;
    const HighlightQuery *outline; // Outline pane query, nullptr if missing or invalid
};

namespace TreeSitterGrammars {

// Grammar and queries (highlighting, outline) for the language, or nullptr if
// the language has no embedded tree-sitter grammar. Queries are compiled on
// the first call and then kept until the program ends. Thread-safe.
const TreeSitterGrammar *forLanguage(const QString &languageId);

} // namespace TreeSitterGrammars
